#include <flowi_core/math_data.h>
#include <flowi_core/style.h>
#include <flowi_core/ui.h>
#include "color.h"
#include "font_private.h"
#include "image_private.h"
#include "internal.h"
#include "style_internal.h"
#include "text.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_mouse_hovering_rect(const FlContext* ctx, FlRect rect) {
    // TODO: Math function
    // TODO: SIMD
    FlVec2 pos = ctx->mouse.pos;

    if (pos.x >= rect.x && pos.y >= rect.y) {
        FlVec2 pos2 = (FlVec2){rect.x + rect.width, rect.y + rect.height};

        if (pos.x < pos2.x && pos.y < pos2.y) {
            return true;
        }
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool item_hoverable(FlContext* ctx, FlRect rect, FlowiID id) {
    if (ctx->hovered_id != 0 && ctx->hovered_id != id) {
        return false;
    }

    if (ctx->active_id != 0 && ctx->active_id != id) {
        return false;
    }

    if (!is_mouse_hovering_rect(ctx, rect)) {
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void set_active_id(FlContext* ctx, FlowiID id) {
    ctx->active_id_is_just_activated = ctx->active_id != id;

    if (ctx->active_id_is_just_activated) {
        ctx->active_id_timer = 0.0f;
        ctx->active_id_has_been_pressed_before = false;
        ctx->active_id_has_been_edited_before = false;
        if (id != 0) {
            ctx->last_active_id = id;
            ctx->last_active_id_timer = 0.0f;
        }
    }

    ctx->active_id = id;
    ctx->active_id_allow_overlap = false;
    ctx->active_id_no_clear_on_focus_loss = false;
    ctx->active_id_has_been_edited_this_frame = false;

    if (id) {
        ctx->active_id_is_alive = id;
        /*
        ctx->active_id_source =
            (ctx->nav_activate_id == id ||
             ctx->nav_input_id == id ||
             ctx->nav_just_tabbed_id == id ||
             ctx->nav_just_moved_to_id  == id) ? InputSource_Nav : InputSource_Mouse;
        */
    }

    // ctx->active_id_window = window;
    // Clear declaration of inputs claimed by the widget
    // (Please note that this is WIP and not all keys/inputs are thoroughly declared by all widgets yet)
    // ctx->active_id_using_mouse_wheel = false;
    // ctx->active_id_using_nav_dir_mask = 0x00;
    // ctx->active_id_using_nav_input_mask = 0x00;
    // ctx->active_id_using_key_input_mask = 0x00;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void clear_active_id(FlContext* ctx) {
    set_active_id(ctx, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool button_behavior(struct FlContext* ctx, FlRect rect, FlowiID id, u32 flags) {
    const MouseState* mouse_state = &ctx->mouse;

    bool pressed = false;
    bool hovered = item_hoverable(ctx, rect, id);

    // Mouse handling
    if (hovered) {
        // printf("is hovered\n");
        //  Poll buttons
        int button_clicked = -1;

        if ((flags & ButtonFlags_MouseButtonLeft) && mouse_state->clicked[0]) {
            button_clicked = 1;
        }

        // print
        // if (button_clicked) {
        //     printf("button clicked %d\n", button_clicked);
        // }

        if (button_clicked != -1 && ctx->active_id != id) {
            if (flags & (ButtonFlags_PressedOnClickRelease | ButtonFlags_PressedOnClickReleaseAnywhere)) {
                set_active_id(ctx, id);
            }

            const bool pressed_on_click = flags & ButtonFlags_PressedOnClick;
            const bool pressed_on_double_click = flags & ButtonFlags_PressedOnDoubleClick;
            const bool double_clicked = mouse_state->double_clicked[button_clicked];

            if (pressed_on_click || (pressed_on_double_click && double_clicked)) {
                pressed = true;
                set_active_id(ctx, id);
            }
        }

        if (flags & ButtonFlags_PressedOnRelease) {
            // int button_released = -1;

            // Repeat mode trumps on release behavior
            // const bool has_repeated_at_least_once = false;
            // if (!has_repeated_at_least_once)

            clear_active_id(ctx);
        }
    }

    if (mouse_state->released[0]) {
        pressed = true;
    }

    // Process while held
    // bool held = false;
    if (ctx->active_id == id) {
        // if (g.active_id_source == InputSource_Mouse)
        // always mouse for now
        if (true) {
            /*
            if (ctx->active_id_is_just_activated) {
                // g.active_id_click_offset = vec2_sub(ctx->mouse_state.pos, rect.min);
            }
            */

            const int mouse_button = ctx->active_id_mouse_button;

            if (ctx->mouse.down[mouse_button]) {
                // held = true;
            } else {
                bool release_in = hovered && (flags & ButtonFlags_PressedOnClickRelease) != 0;
                bool release_anywhere = (flags & ButtonFlags_PressedOnClickReleaseAnywhere) != 0;
                const float repeat_delay = 0.025f;  // TODO: Configurable value
                // if ((release_in || release_anywhere) && !ctx->drag_drop_active) {
                if (release_in || release_anywhere) {
                    // Report as pressed when releasing the mouse (this is the most common path)
                    bool is_double_click_release =
                        (flags & ButtonFlags_PressedOnDoubleClick) && ctx->mouse.down_was_double_click[mouse_button];
                    // Repeat mode trumps <on release>
                    bool is_repeating_already =
                        (flags & ButtonFlags_Repeat) && ctx->mouse.down_duration_prev[mouse_button] >= repeat_delay;
                    if (!is_double_click_release && !is_repeating_already)
                        pressed = true;
                }
                clear_active_id(ctx);
            }
        }
        // if (!(flags & ButtonFlags_NoNavFocus))
        //     ctx->nav_disable_highlight = true;
    }
    /*
    else if (g.active_id_source == im_gui_input_source_nav) {
        // When activated using Nav, we hold on the ActiveID until activation button is released
        if (ctx->nav_activate_down_id != id)
            clear_active_id();
    }
    if (pressed) {
        g.active_id_has_been_pressed_before = true;
    }
    */

    if (pressed) {
        printf("pressed\n");
    }

    return pressed;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// taken from:
// http://blade.nagaokaut.ac.jp/cgi-bin/scat.rb/ruby/ruby-talk/142054
//
// djb  :: 99.8601 percent coverage (60 collisions out of 42884)
// elf  :: 99.5430 percent coverage (196 collisions out of 42884)
// sdbm :: 100.0000 percent coverage (0 collisions out of 42884) (this is the algo used)
// ...

static u32 str_hash(const char* string, int len) {
    u32 hash = 0;

    const u8* str = (const u8*)string;

    for (int i = 0; i < len; ++i) {
        u32 c = *str++;
        hash = c + (hash << 6) + (hash << 16) - hash;
    }

    return hash & 0x7FFFFFFF;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set position for the next ui-element (this is used when [LayoutMode::Manual] is used)

void fl_ui_set_pos_impl(struct FlContext* ctx, FlVec2 pos) {
    ctx->cursor = pos;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void keep_id_alive(struct FlContext* ctx, FlowiID id) {
    if (ctx->active_id == id) {
        ctx->active_id_is_alive = true;
    }

    if (ctx->active_id_previous_frame == id) {
        ctx->active_id_previous_frame_is_alive = true;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool item_add(struct FlContext* ctx, FlowiID id, FlRect rect) {
    ctx->last_item_data.id = id;
    ctx->last_item_data.rect = rect;
    ctx->last_item_data.status_flags = 0;

    if (id != 0) {
        keep_id_alive(ctx, id);
    }

    return is_mouse_hovering_rect(ctx, rect);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum ButtonState {
    ButtonState_Default,
    ButtonState_Fading,
} ButtonState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ButtonStateData {
    ButtonState state;
    FColor start_color;
    FColor end_color;
    FColor current_color;
    float fade_progress;
} ButtonStateData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Updates button state

static void button_state_update(ButtonStateData* state, const float delta_time, bool is_hovered) {
    switch (state->state) {
        case ButtonState_Default: {
            if (is_hovered) {
                state->fade_progress = 0.0f;
                state->current_color = state->start_color;
            } else {
                state->state = ButtonState_Fading;
            }

            break;
        }
        case ButtonState_Fading: {
            if (is_hovered) {
                state->state = ButtonState_Default;
                state->fade_progress = 0.0f;
                state->current_color = state->start_color;
                break;
            } else {
                state->current_color = FColor_lerp(state->start_color, state->end_color, state->fade_progress);
                state->fade_progress += delta_time;
                if (state->fade_progress >= 1.0f) {
                    state->state = ButtonState_Default;
                    state->current_color = state->end_color;
                }
            }

            break;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Push button widget that returns true if user has pressed it

bool fl_ui_push_button_impl(struct FlContext* ctx, FlString text) {
    FlStyle style = fl_style_get_current(ctx);

    Utf8Result utf8_res = Utf8_to_codepoints_u32(&ctx->frame_allocator, (u8*)text.str, text.len);

    if (utf8_res.error == FlError_Utf8Malformed) {
        // TODO: Proper error
        printf("illegal string\n");
        return false;
    }

    FlowiID id = (FlowiID)str_hash(text.str, text.len);

    FlVec2 pos = ctx->cursor;

    pos.x += style.margin[FlCorner_TopLeft];
    pos.y += style.margin[FlCorner_TopRight];

    // TODO: text alignment (currently assume text should be centered)

    // Calculate the size of the text
    FlIVec2 text_size = Font_calc_text_size(ctx, utf8_res.codepoints, utf8_res.len);

    // TODO: Handle corners correctly
    uint16_t x_padding = style.padding[FlCorner_TopRight] + style.padding[FlCorner_BottomRight];
    uint16_t y_padding = style.padding[FlCorner_TopLeft] + style.padding[FlCorner_BottomLeft];
    FlVec2 rect_size = {text_size.x + x_padding, text_size.y + y_padding};

    // static bool button_behavior(struct FlContext * ctx, FlRect rect, FlowiID id, u32 flags);
    FlRect rect = {.x = (int)pos.x, .y = (int)pos.y, .width = (int)rect_size.x, .height = (int)rect_size.y};

    // TODO: This is being called inside button behaviour also, maybe we can do this in a different way?

    // TODO: Append "_button" to the end of the string for hashing id

    ButtonStateData* button_state = hashmap_get(&ctx->widget_states, text.str, text.len);

    if (!button_state) {
        // TODO: Custom allocator
        button_state = calloc(1, sizeof(ButtonStateData));
        button_state->start_color = FColor_new_rgb(0.3f, 0.3f, 0.3f);
        button_state->end_color = FColor_new_rgb(0.2f, 0.2f, 0.2f);
        hashmap_put(&ctx->widget_states, text.str, text.len, button_state);
    } else {
        button_state_update(button_state, ctx->delta_time, item_hoverable(ctx, rect, id));
    }

    // Add rect for rendering
    {
        PrimitiveRect* prim = Primitive_alloc_rect(ctx->global);
        memset(prim, 0, sizeof(PrimitiveRect));
        prim->pos = pos;
        prim->size = rect_size;
        prim->color = FColor_to_u32(button_state->current_color);
    }

    // Add text for rendering
    {
        PrimitiveText* prim = Primitive_alloc_text(ctx->global);

        prim->font = ctx->current_font;
        prim->position.x = pos.x;  // + style.padding[FlCorner_TopLeft];
        prim->position.y = pos.y;  //+ style.padding[FlCorner_TopRight];
        prim->font_size = ctx->current_font_size != 0 ? ctx->current_font_size : ctx->current_font->default_size;
        prim->codepoints = utf8_res.codepoints;
        prim->codepoint_count = utf8_res.len;
        prim->position_index = 0;  // TODO: Fixme
    }

    // TODO: Move cursor directly downwards with style curently, need to configure?
    ctx->cursor.y += text_size.y + y_padding + style.margin[FlCorner_TopLeft] + style.margin[FlCorner_TopRight];

    if (!item_add(ctx, id, rect)) {
        return false;
    }

    return button_behavior(ctx, rect, id, ButtonFlags_PressedOnClickRelease | ButtonFlags_MouseButtonLeft);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_ui_push_button_with_icon_impl(struct FlContext* ctx, FlString text, FlImage image, FlVec2 text_pos,
                                      float image_scale) {
    Utf8Result utf8_res = Utf8_to_codepoints_u32(&ctx->frame_allocator, (u8*)text.str, text.len);

    if (utf8_res.error == FlError_Utf8Malformed) {
        // TODO: Proper error
        printf("illegal string\n");
        return false;
    }

    ImagePrivate* image_data = (ImagePrivate*)Handles_get_data(&ctx->global->image_handles, image);
    FL_UNUSED(image_data);

    if (!image) {
        // printf("Illegal image id %lx\n", (u64)image);
        return false;
    }

    // Calculate the size of the text
    FlIVec2 text_size = Font_calc_text_size(ctx, utf8_res.codepoints, utf8_res.len);

    FlIVec2 image_size = {
        (int)(image_data->info.width * image_scale),
        (int)(image_data->info.height * image_scale),
    };

    // adjust the text a bit to center with image
    int text_offset = (image_size.y - text_size.y);  // * 0.5f;

    FlVec2 pos = ctx->cursor;

    // Add image for rendering
    {
        PrimitiveImage* prim = Primitive_alloc_image(ctx->global);
        prim->image = image_data;
        prim->position = pos;
        prim->size.x = image_size.x;
        prim->size.y = image_size.y;
    }

    // Add text for rendering
    {
        PrimitiveText* prim = Primitive_alloc_text(ctx->global);

        prim->font = ctx->current_font;
        prim->position.x = pos.x + text_pos.x;
        prim->position.y = pos.y + text_pos.y + text_offset;
        prim->font_size = ctx->current_font_size != 0 ? ctx->current_font_size : ctx->current_font->default_size;
        prim->codepoints = utf8_res.codepoints;
        prim->codepoint_count = utf8_res.len;
        prim->position_index = 0;  // TODO: Fixme
    }

    // TODO: Include style in position calculation

    // Based on the current the the position where we should place the image and the text
    // FlVec2 pos = Style_get_position(ctx);

    return false;
}
