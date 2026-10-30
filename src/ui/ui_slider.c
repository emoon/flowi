// Slider family: log/linear value math, the shared slider_impl, value formatting, and the float/int public
// wrappers.

#include "ui.h"
#include "ui_private.h"
#include "ui_widget_internal.h"
#include "ui_variant_helpers.h"
#include "ui_tokens.h"
#include "layout.h"
#include <core/core.h>
#include <core/math.h>
#include <core/log.h>
#include <core/sprintf.h>
#include <external/clay/clay.h>
#include <math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Slider helper functions

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline float slider_value_to_log_normalized(float value, float min, float max) {
    float log_min = log10f(fmaxf(min, UI_SLIDER_LOG_EPSILON));
    float log_max = log10f(fmaxf(max, UI_SLIDER_LOG_EPSILON));
    float log_value = log10f(fmaxf(value, UI_SLIDER_LOG_EPSILON));
    return inv_lerp(log_min, log_max, log_value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline float slider_log_normalized_to_value(float normalized, float min, float max) {
    float log_min = log10f(fmaxf(min, UI_SLIDER_LOG_EPSILON));
    float log_max = log10f(fmaxf(max, UI_SLIDER_LOG_EPSILON));
    float log_result = lerp(log_min, log_max, normalized);
    return powf(10.0f, log_result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// vertical: up/down adjust the value and left/right navigate away; horizontal is the mirror image.

static float slider_update_value_from_input(float current_normalized, float normalized_step, FlSignal signal,
                                            FlUiCtx* self, bool* value_modified, bool vertical) {
    float normalized = current_normalized;

    if (signal.focused) {
        float adjustment = 0.0f;
        bool fast_mode = self->input_state.keyboard.modifiers & (1 << 0); // Shift

        if (self->input_state.keyboard.keys[FlKey_Home]) {
            normalized = 0.0f;
            *value_modified = true;
        } else if (self->input_state.keyboard.keys[FlKey_End]) {
            normalized = 1.0f;
            *value_modified = true;
        } else if (vertical) {
            if (self->nav_frame.nav_up) {
                adjustment = normalized_step;
                self->nav_frame.nav_up = false; // Consume to prevent spatial navigation
            } else if (self->nav_frame.nav_down) {
                adjustment = -normalized_step;
                self->nav_frame.nav_down = false; // Consume to prevent spatial navigation
            }
            // nav_left and nav_right pass through for focus navigation
        } else {
            if (self->nav_frame.nav_left) {
                adjustment = -normalized_step;
                self->nav_frame.nav_left = false; // Consume to prevent spatial navigation
            } else if (self->nav_frame.nav_right) {
                adjustment = normalized_step;
                self->nav_frame.nav_right = false; // Consume to prevent spatial navigation
            }
            // nav_up and nav_down pass through for focus navigation
        }

        if (fast_mode) {
            adjustment *= 10.0f;
        }

        if (adjustment != 0.0f) {
            normalized = clamp(normalized + adjustment, 0.0f, 1.0f);
            *value_modified = true;
        }
    }

    // Analog stick only - the D-pad is already handled by the nav keys above.
    if (signal.focused && self->input_state.gamepads[0].connected) {
        GamepadState* gamepad = &self->input_state.gamepads[0];
        float adjustment = 0.0f;

        if (vertical) {
            if (fabsf(gamepad->axes[FlGamepadAxis_Left_Y]) > gamepad->dead_zone) {
                // Invert Y axis (stick up = positive = increase value)
                adjustment = -gamepad->axes[FlGamepadAxis_Left_Y] * normalized_step * UI_SLIDER_ANALOG_SENSITIVITY;
                self->nav_frame.nav_up = false;
                self->nav_frame.nav_down = false;
            }
        } else {
            if (fabsf(gamepad->axes[FlGamepadAxis_Left_X]) > gamepad->dead_zone) {
                adjustment = gamepad->axes[FlGamepadAxis_Left_X] * normalized_step * UI_SLIDER_ANALOG_SENSITIVITY;
                self->nav_frame.nav_left = false;
                self->nav_frame.nav_right = false;
            }
        }

        if (adjustment != 0.0f) {
            normalized = clamp(normalized + adjustment, 0.0f, 1.0f);
            *value_modified = true;
        }
    }

    return normalized;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Shared slider body. is_int only governs how the display value is formatted: int sliders apply the format
// string to the rounded integer (see ui_slider_format_value).
static FlSignal slider_impl(FlString label, float* value, float min, float max, FlSliderConfig cfg, bool is_int) {
    FlUiCtx* self = g_ui_ctx;

    *value = clamp(*value, min, max);

    float normalized;
    if (cfg.logarithmic) {
        normalized = slider_value_to_log_normalized(*value, min, max);
    } else {
        normalized = inv_lerp(min, max, *value);
    }

    float step = cfg.step;
    if (step <= 0.0f) {
        step = (max - min) * 0.01f; // Default: 1% of range
    }
    float normalized_step = step / (max - min);

    // Slider is custom-coloured (track fill / thumb below).
    FlItemFlags flags = FlItemFlags_clickable | FlItemFlags_draggable | FlItemFlags_focusable
                        | FlItemFlags_scroll_target | FlItemFlags_hot_animation | FlItemFlags_active_animation
                        | FlItemFlags_focus_animation;
    UiWidgetState w = ui_widget_begin(label, cfg.widget, flags);
    FlLayoutId slider_id = w.id;
    FlSignal signal = w.signal;
    ItemState* item = w.item;
    const FlThemeColors* theme = &w.theme;

    bool value_modified = false;

    // Track / thumb geometry, each config-driven with 0 meaning "keep the default": an 8px bar with a 24px
    // round thumb. A track thinner than the thumb centers vertically against it (the row's child alignment
    // does that), so the row height - and with it layout and hit-testing - is the thumb size either way.
    const float thumb_size = cfg.thumb_size > 0.0f ? cfg.thumb_size : UI_SLIDER_THUMB_WIDTH;
    const float track_height = cfg.track_height > 0.0f ? cfg.track_height : UI_SLIDER_TRACK_HEIGHT;

    // The visual layout is [filled grow][thumb fixed][unfilled grow], so the thumb travels over
    // (track_width - thumb_size) pixels.
    float track_width = item->state.size.x;
    float usable_width = track_width - thumb_size;

    // Drag, or click-on-track to jump. A thumb as wide as its own row leaves no travel span to map over.
    if ((signal.dragging || (signal.clicked && signal.hovered)) && usable_width > 0.0f) {
        // Mouse position relative to the track, offset by half a thumb, i.e. where the thumb center would be.
        float mouse_x_relative = self->input_state.mouse.x - ITEM_LEFT(item) - (thumb_size / 2.0f);

        normalized = clamp(mouse_x_relative / usable_width, 0.0f, 1.0f);
        value_modified = true;
    }

    if (signal.wheel_delta.y != 0.0f) {
        float wheel_adjustment = (signal.wheel_delta.y > 0.0f) ? normalized_step : -normalized_step;
        normalized = clamp(normalized + wheel_adjustment, 0.0f, 1.0f);
        value_modified = true;
    }

    normalized
        = slider_update_value_from_input(normalized, normalized_step, signal, self, &value_modified, cfg.vertical);

    // Only update value if user interacted (prevents precision drift)
    if (value_modified) {
        float new_value;
        if (cfg.logarithmic) {
            new_value = slider_log_normalized_to_value(normalized, min, max);
        } else {
            new_value = lerp(min, max, normalized);
        }
        *value = clamp(new_value, min, max);
    }

    FlString value_str = ui_slider_format_value(self->render_frame_allocator, cfg.format, *value, is_int);

    FlColor track_unfilled = theme->surface.elevated;
    FlColor base_fill = theme->text.primary;
    FlColor text_color = cfg.widget.text_color.a > 0 ? cfg.widget.text_color : theme->text.primary;

    FlColor fill_color = ui_apply_state_color(base_fill, item);

    FlColor base_thumb = theme->text.primary;
    FlColor thumb_color = ui_apply_state_color(base_thumb, item);

    ui_layout_area(.sizing = { sizing_grow(0), sizing_fixed(FlWidgetHeight_LG) },
                   .direction = LayoutDirection_TopToBottom, .child_gap = FlSpacing_SM) {
        ui_layout_area(.sizing = { sizing_grow(0), sizing_fit() }, .direction = LayoutDirection_LeftToRight,
                       .child_alignment = ui_align(Left, Center)) {
            if (label.length > 0) {
                ui_text_styled(label, .color = text_color);
            }

            // Spacer
            ui_layout_area(.sizing = { sizing_grow(), sizing_fit() }) {}

            ui_text_styled(value_str, .color = text_color);
        }

        // The travel span is derived from the track size the item carried over from the previous frame.
        float filled_width = usable_width * normalized;
        float unfilled_width = usable_width * (1.0f - normalized);

        if (filled_width < 0.0f)
            filled_width = 0.0f;
        if (unfilled_width < 0.0f)
            unfilled_width = 0.0f;

        ui_layout_area_id(slider_id, .sizing = { sizing_grow(200, 1000), sizing_fixed(thumb_size) },
                          .direction = LayoutDirection_LeftToRight, .child_alignment = ui_align(Left, Center),
                          .color = UI_HIT_TEST_FILL(1)) {
            ui_layout_area(.sizing = { sizing_fixed(filled_width), sizing_fixed(track_height) },
                           .radius = fl_corner_radius_left(FlRadius_Full), .color = fill_color) {}

            ui_layout_area(.sizing = { sizing_fixed(thumb_size), sizing_fixed(thumb_size) },
                           .radius = corner_radius(FlRadius_Full), .color = thumb_color) {}

            ui_layout_area(.sizing = { sizing_fixed(unfilled_width), sizing_fixed(track_height) },
                           .radius = fl_corner_radius_right(FlRadius_Full), .color = track_unfilled) {}
        }
    }

    return signal;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString ui_slider_format_value(FlArena* arena, const char* format, float value, bool is_int) {
    if (is_int) {
        // Apply the format to the rounded integer; a NULL format falls back to a bare integer.
        return fl_sprintf_arena(arena, format ? format : "%d", (int)roundf(value));
    }
    return fl_sprintf_arena(arena, format ? format : "%.2f", value);
}

FlSignal ui_slider_float_impl(FlString label, float* value, float min, float max, FlSliderConfig cfg) {
    return slider_impl(label, value, min, max, cfg, false);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
FlSignal ui_slider_int_impl(FlString label, int* value, int min, int max, FlSliderConfig cfg) {
    // Drive the shared slider over a float mirror of the int, formatting the display against the integer value
    // (is_int = true).
    float float_value = (float)(*value);
    FlSignal signal = slider_impl(label, &float_value, (float)min, (float)max, cfg, true);
    *value = clamp((int)roundf(float_value), min, max);
    return signal;
}
