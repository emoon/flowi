// Hit testing: per-frame refresh of the interactive item registry from the resolved layout.
//
// Only ids already present in UiShared.item_state are touched (ui_item_behavior creates them), so text, images
// and other non-interactive elements never enter hit testing. Clip rects are tracked through the render
// command stream's scissor pairs, since Clay culls against the screen and not against an enclosing clip.

#include "ui_hit_test.h"
#include "animation.h"
#include <core/hashmap.h>
#include <core/math.h>
#include <core/types.h>
#include <math.h>

static void update_item_animations(ItemState* item, float hover_rate, float press_rate, float focus_rate) {
    if (item->flags & FlItemFlags_hot_animation) {
        const f32 target = item->state.hot ? 1.0f : 0.0f;
        item->state.hot_t = anim_exp_toward(item->state.hot_t, target, hover_rate);
    }

    if (item->flags & FlItemFlags_active_animation) {
        const f32 target = item->state.active ? 1.0f : 0.0f;
        item->state.active_t = anim_exp_toward(item->state.active_t, target, press_rate);
    }

    if (item->flags & FlItemFlags_focus_animation) {
        const f32 target = item->state.focused ? 1.0f : 0.0f;
        item->state.focused_t = anim_exp_toward(item->state.focused_t, target, focus_rate);
    }
}

static bool ui_point_in_rect(Vec2 point, Vec2 min, Vec2 max) {
    return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
}

// The scissor rect in force where a render command was emitted: the intersection of every clip container that
// encloses it. Unbounded when no scissor is open around the command.

typedef struct HitClip {
    float min_x;
    float min_y;
    float max_x;
    float max_y;
} HitClip;

#define HIT_CLIP_UNBOUNDED ((HitClip) { -INFINITY, -INFINITY, INFINITY, INFINITY })
// Maximum clip container nesting the hit-test walk tracks.
#define UI_HIT_CLIP_STACK_MAX 32

bool ui_item_hit_contains(const ItemState* item, Vec2 point) {
    return ui_point_in_rect(point, item->hit_min, item->hit_max);
}

static void item_set_geometry(FlUiCtx* self, ItemState* item, const Clay_BoundingBox* box, HitClip clip) {
    item->state.pos.x = box->x;
    item->state.pos.y = box->y;
    item->state.size.x = box->width;
    item->state.size.y = box->height;
    item->state.found = 1;

    // What survives the clip chain, which is also what the screen test below is applied to: an element cut away
    // entirely by an ancestor is not visible, wherever its own rect happens to sit.
    const float left = fmaxf(item->state.pos.x, clip.min_x);
    const float top = fmaxf(item->state.pos.y, clip.min_y);
    const float right = fminf(item->state.pos.x + item->state.size.x, clip.max_x);
    const float bottom = fminf(item->state.pos.y + item->state.size.y, clip.max_y);
    const bool has_area = right >= left && bottom >= top;
    const bool off_screen = right < 0 || left > self->layout_width || bottom < 0 || top > self->layout_height;
    item->state.visible = (has_area && !off_screen) ? 1 : 0;

    item->hit_min = (Vec2) { left, top };
    item->hit_max = (Vec2) { right, bottom };
}

// While something is active, only that item may become hot.
static bool item_may_become_hot(const FlUiCtx* self, u32 id) {
    return self->active_id == 0 || self->active_id == id;
}

static void item_set_interaction(FlUiCtx* self, ItemState* item, u32 id) {
    const Vec2 mouse = { self->input_state.mouse.x, self->input_state.mouse.y };
    if (ui_item_hit_contains(item, mouse) && item_may_become_hot(self, id)) {
        self->next_hot_id = id;
    }

    item->state.hot = (self->hot_id == id) ? 1 : 0;
    item->state.active = (self->active_id == id) ? 1 : 0;

    item->was_focused = item->state.focused;
    item->state.focused = (self->focused_id == id) ? 1 : 0;
}

static void item_state_update(FlUiCtx* self, const Clay_RenderCommand* command, HitClip clip, const u64 pass,
                              const float dt) {
    const f32 hover_rate = anim_exp_ease_out_rate(ANIM_SPEED_HOVER, dt);
    const f32 press_rate = anim_exp_ease_out_rate(ANIM_SPEED_PRESS, dt);
    const f32 focus_rate = anim_exp_ease_out_rate(ANIM_SPEED_FOCUS, dt);

    // Only items explicitly created as interactive (e.g. by ui_item_behavior) have an entry; skipping the rest
    // keeps text, images and other non-interactive elements out of hit testing.
    ItemState* item = hashmap_get(&self->shared->item_state, command->id);

    // Clay gives an element's background, image, border and clip commands the same id; the first one seen
    // carries the clip the element sits under.
    if (item == nullptr || item->hit_pass == pass) {
        return;
    }
    item->hit_pass = pass;

    item->frame = self->shared->item_epoch;
    item_set_geometry(self, item, &command->boundingBox, clip);
    item_set_interaction(self, item, command->id);

    update_item_animations(item, hover_rate, press_rate, focus_rate);
}

typedef struct HitClipStack {
    HitClip entries[UI_HIT_CLIP_STACK_MAX];
    int depth;
    // Pushes dropped at capacity, matched against their pops.
    int ignored_pushes;
} HitClipStack;

static HitClip hit_clip_current(const HitClipStack* stack) {
    return stack->depth > 0 ? stack->entries[stack->depth - 1] : HIT_CLIP_UNBOUNDED;
}

static void hit_clip_pop(HitClipStack* stack) {
    if (stack->ignored_pushes > 0) {
        stack->ignored_pushes--;
    } else if (stack->depth > 0) {
        stack->depth--;
    }
}

static void hit_clip_push(HitClipStack* stack, HitClip clip, Clay_BoundingBox box) {
    if (stack->depth >= UI_HIT_CLIP_STACK_MAX) {
        stack->ignored_pushes++;
        return;
    }
    stack->entries[stack->depth++] = (HitClip) {
        .min_x = fmaxf(clip.min_x, box.x),
        .min_y = fmaxf(clip.min_y, box.y),
        .max_x = fminf(clip.max_x, box.x + box.width),
        .max_y = fminf(clip.max_y, box.y + box.height),
    };
}

// Overlay markers carry the element's id and no geometry; the element's own commands follow them.
static bool is_overlay_marker(Clay_RenderCommandType type) {
    return type == CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_START || type == CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_END;
}

void ui_hit_test_frame(FlUiCtx* self, Clay_RenderCommandArray commands, float dt) {
    self->next_hot_id = 0;
    const u64 pass = ++self->shared->hit_test_pass;

    // Commands carry each element's full bounding box, not its clipped rect.
    HitClipStack clips = { 0 };

    for_count(i, commands.length) {
        const Clay_RenderCommand* command = Clay_RenderCommandArray_Get(&commands, i);

        // A scissor-end command carries a zeroed bounding box and a derived id, so there is nothing to hit-test.
        if (command->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
            hit_clip_pop(&clips);
            continue;
        }

        if (is_overlay_marker(command->commandType)) {
            continue;
        }

        const HitClip clip = hit_clip_current(&clips);
        item_state_update(self, command, clip, pass, dt);

        // A clip element's scissor-start carries the element's own id and box; for an element with no background
        // it is the only command it gets.
        if (command->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
            hit_clip_push(&clips, clip, command->boundingBox);
        }
    }
}
