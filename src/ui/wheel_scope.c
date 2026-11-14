#include "wheel_scope.h"
#include <core/hashmap.h>
#include "ui_hit_test.h"
#include "ui_private.h"

#define WHEEL_SCOPE_MAX_DEPTH 16

static u32 wheel_scope_parent(const FlUiCtx* ctx, u32 element_id) {
    const WheelScope* scope = hashmap_get(&ctx->shared->wheel_scopes, element_id);
    return scope != nullptr ? scope->parent_id : 0;
}

void wheel_scope_begin(FlUiCtx* ctx, u32 element_id, bool scrolls) {
    WheelScope entry = { .parent_id = ctx->wheel_scope_id, .scrolls = scrolls };
    WheelScope* scope = hashmap_get(&ctx->shared->wheel_scopes, element_id);
    if (scope != nullptr) {
        *scope = entry;
    } else {
        hashmap_insert(&ctx->shared->wheel_scopes, element_id, entry);
    }
    ctx->wheel_scope_id = element_id;
}

void wheel_scope_end(FlUiCtx* ctx) {
    ctx->wheel_scope_id = wheel_scope_parent(ctx, ctx->wheel_scope_id);
}

// Pointer is over the container's clip-cut rect from the previous frame's hit test. A container not yet in
// item_state (first frame) is not hovered.
bool wheel_scope_hovered(const FlUiCtx* ctx, u32 element_id) {
    const ItemState* item = hashmap_get(&ctx->shared->item_state, element_id);
    if (item == nullptr || !item->state.found) {
        return false;
    }

    return ui_item_hit_contains(item, (Vec2) { ctx->input_state.mouse.x, ctx->input_state.mouse.y });
}

static bool wheel_scope_is_ancestor(const FlUiCtx* ctx, u32 ancestor_id, u32 element_id) {
    u32 parent_id = element_id;
    for_count(i, WHEEL_SCOPE_MAX_DEPTH) {
        parent_id = wheel_scope_parent(ctx, parent_id);
        if (parent_id == 0) {
            return false;
        }
        if (parent_id == ancestor_id) {
            return true;
        }
    }
    return false;
}

// The wheel goes to the innermost hovered scope only: a scope takes it when hovered and no scope nested inside it
// is also hovered.
static bool wheel_scope_has_hovered_descendant(const FlUiCtx* ctx, u32 element_id) {
    for_each_hashmap(&ctx->shared->wheel_scopes, other_id, other) {
        bool takes_wheel = other->scrolls && wheel_scope_hovered(ctx, *other_id);
        if (takes_wheel && wheel_scope_is_ancestor(ctx, element_id, *other_id)) {
            return true;
        }
    }
    return false;
}

bool wheel_scope_owns_wheel(const FlUiCtx* ctx, u32 element_id) {
    return wheel_scope_hovered(ctx, element_id) && !wheel_scope_has_hovered_descendant(ctx, element_id);
}
