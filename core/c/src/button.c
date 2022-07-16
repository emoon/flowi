#include "color.h"
#include "internal.h"
#include "text.h"

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
    style.border.radius[0].typ = FlLengthPercent_Percent;
    style.border.radius[0].value = 40.0f;
    style.border.radius[1].typ = FlLengthPercent_Percent;
    style.border.radius[1].value = 40.0f;
    style.border.radius[2].typ = FlLengthPercent_Percent;
    style.border.radius[2].value = 40.0f;
    style.border.radius[3].typ = FlLengthPercent_Percent;
    style.border.radius[3].value = 40.0f;

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
        button_state->start_color = FColor_new_rgb(0.8f, 0.8f, 0.8f);
        button_state->end_color = FColor_new_rgb(0.6f, 0.6f, 0.6f);
        hashmap_put(&ctx->widget_states, text.str, text.len, button_state);
    } else {
        button_state_update(button_state, ctx->delta_time, item_hoverable(ctx, rect, id));
    }

    Layer* layer = ctx_get_active_layer(ctx);

    // Add rect for rendering
    {
        PrimitiveRect* prim = Primitive_alloc_rect(layer);
        memset(prim, 0, sizeof(PrimitiveRect));
        prim->border = style.border;
        prim->pos = pos;
        prim->size = rect_size;
        prim->color = FColor_to_u32(button_state->current_color);
    }

    // Add text for rendering
    {
        PrimitiveText* prim = Primitive_alloc_text(layer);

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
