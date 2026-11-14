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

static inline float slider_value_to_log_normalized(float value, float min, float max) {
    float log_min = log10f(fmaxf(min, UI_SLIDER_LOG_EPSILON));
    float log_max = log10f(fmaxf(max, UI_SLIDER_LOG_EPSILON));
    float log_value = log10f(fmaxf(value, UI_SLIDER_LOG_EPSILON));
    return inv_lerp(log_min, log_max, log_value);
}

static inline float slider_log_normalized_to_value(float normalized, float min, float max) {
    float log_min = log10f(fmaxf(min, UI_SLIDER_LOG_EPSILON));
    float log_max = log10f(fmaxf(max, UI_SLIDER_LOG_EPSILON));
    float log_result = lerp(log_min, log_max, normalized);
    return powf(10.0f, log_result);
}

// A slider's value as it moves through one frame: pointer, wheel, keys and gamepad each nudge `normalized`, and
// the value is only recomputed from it when some input touched it (which keeps an untouched value free of
// round-trip precision drift).
typedef struct SliderModel {
    float value;
    float min;
    float max;
    bool logarithmic;
    float normalized;
    float normalized_step;
    bool modified;
} SliderModel;

// Track / thumb geometry, each config-driven with 0 meaning "keep the default": an 8px bar with a 24px round
// thumb. A track thinner than the thumb centers vertically against it (the row's child alignment does that), so
// the row height (and with it layout and hit-testing) is the thumb size either way.
typedef struct SliderGeometry {
    float thumb_size;
    float track_height;
    // The visual layout is [filled grow][thumb fixed][unfilled grow], so the thumb travels over
    // (track_width - thumb_size) pixels. Derived from the track size the item carried over from the previous frame.
    float usable_width;
} SliderGeometry;

static SliderModel slider_model_new(float value, float min, float max, FlSliderConfig cfg) {
    SliderModel m = { .value = clamp(value, min, max), .min = min, .max = max, .logarithmic = cfg.logarithmic };

    if (m.logarithmic) {
        m.normalized = slider_value_to_log_normalized(m.value, min, max);
    } else {
        m.normalized = inv_lerp(min, max, m.value);
    }

    float step = cfg.step;
    if (step <= 0.0f) {
        step = (max - min) * 0.01f; // Default: 1% of range
    }
    m.normalized_step = step / (max - min);
    return m;
}

static void slider_model_set_normalized(SliderModel* m, float normalized) {
    m->normalized = normalized;
    m->modified = true;
}

static void slider_model_nudge(SliderModel* m, float adjustment) {
    slider_model_set_normalized(m, clamp(m->normalized + adjustment, 0.0f, 1.0f));
}

static void slider_model_commit(SliderModel* m) {
    if (!m->modified) {
        return;
    }

    float new_value;
    if (m->logarithmic) {
        new_value = slider_log_normalized_to_value(m->normalized, m->min, m->max);
    } else {
        new_value = lerp(m->min, m->max, m->normalized);
    }
    m->value = clamp(new_value, m->min, m->max);
}

static SliderGeometry slider_geometry(FlSliderConfig cfg, const ItemState* item) {
    SliderGeometry g = {
        .thumb_size = cfg.thumb_size > 0.0f ? cfg.thumb_size : UI_SLIDER_THUMB_WIDTH,
        .track_height = cfg.track_height > 0.0f ? cfg.track_height : UI_SLIDER_TRACK_HEIGHT,
    };
    g.usable_width = item->state.size.x - g.thumb_size;
    return g;
}

static bool slider_jump_requested(FlSignal signal) {
    return signal.dragging || (signal.clicked && signal.hovered);
}

// Drag, or click-on-track to jump. A thumb as wide as its own row leaves no travel span to map over.
static void slider_apply_pointer(SliderModel* m, FlSignal signal, const ItemState* item, SliderGeometry g) {
    if (!slider_jump_requested(signal) || g.usable_width <= 0.0f) {
        return;
    }

    // Mouse position relative to the track, offset by half a thumb, i.e. where the thumb center would be.
    float mouse_x_relative = g_ui_ctx->input_state.mouse.x - ITEM_LEFT(item) - (g.thumb_size / 2.0f);
    slider_model_set_normalized(m, clamp(mouse_x_relative / g.usable_width, 0.0f, 1.0f));
}

static void slider_apply_wheel(SliderModel* m, FlSignal signal) {
    if (signal.wheel_delta.y != 0.0f) {
        slider_model_nudge(m, (signal.wheel_delta.y > 0.0f) ? m->normalized_step : -m->normalized_step);
    }
}

// Each pair consumes the nav flags it acts on to prevent spatial navigation; the perpendicular pair passes
// through for focus navigation.
static float slider_nav_vertical(FlNavFrame* nav, float step) {
    if (nav->nav_up) {
        nav->nav_up = false;
        return step;
    }
    if (nav->nav_down) {
        nav->nav_down = false;
        return -step;
    }
    return 0.0f;
}

static float slider_nav_horizontal(FlNavFrame* nav, float step) {
    if (nav->nav_left) {
        nav->nav_left = false;
        return -step;
    }
    if (nav->nav_right) {
        nav->nav_right = false;
        return step;
    }
    return 0.0f;
}

// vertical: up/down adjust the value and left/right navigate away; horizontal is the mirror image.
static void slider_apply_keys(SliderModel* m, FlUiCtx* self, bool vertical) {
    if (self->input_state.keyboard.keys[FlKey_Home]) {
        slider_model_set_normalized(m, 0.0f);
        return;
    }
    if (self->input_state.keyboard.keys[FlKey_End]) {
        slider_model_set_normalized(m, 1.0f);
        return;
    }

    float adjustment = vertical ? slider_nav_vertical(&self->nav_frame, m->normalized_step)
                                : slider_nav_horizontal(&self->nav_frame, m->normalized_step);
    bool fast_mode = self->input_state.keyboard.modifiers & (1 << 0); // Shift
    if (fast_mode) {
        adjustment *= 10.0f;
    }
    if (adjustment != 0.0f) {
        slider_model_nudge(m, adjustment);
    }
}

// Analog stick only; the D-pad is already handled by the nav keys.
static void slider_apply_gamepad(SliderModel* m, FlUiCtx* self, bool vertical) {
    const GamepadState* gamepad = &self->input_state.gamepads[0];
    if (!gamepad->connected) {
        return;
    }

    // Stick up = positive = increase value, so the Y axis is inverted.
    float axis = vertical ? -gamepad->axes[FlGamepadAxis_Left_Y] : gamepad->axes[FlGamepadAxis_Left_X];
    if (!(fabsf(axis) > gamepad->dead_zone)) {
        return;
    }

    if (vertical) {
        self->nav_frame.nav_up = false;
        self->nav_frame.nav_down = false;
    } else {
        self->nav_frame.nav_left = false;
        self->nav_frame.nav_right = false;
    }

    float adjustment = axis * m->normalized_step * UI_SLIDER_ANALOG_SENSITIVITY;
    if (adjustment != 0.0f) {
        slider_model_nudge(m, adjustment);
    }
}

static void slider_apply_focused_input(SliderModel* m, FlUiCtx* self, bool vertical) {
    slider_apply_keys(m, self, vertical);
    slider_apply_gamepad(m, self, vertical);
}

static void slider_header(FlString label, FlString value_str, FlColor text_color) {
    ui_layout_area(.sizing = { sizing_grow(0), sizing_fit() }, .direction = LayoutDirection_LeftToRight,
                   .child_alignment = ui_align(Left, Center)) {
        if (label.length > 0) {
            ui_text_styled(label, .color = text_color);
        }

        // Spacer
        ui_layout_area(.sizing = { sizing_grow(), sizing_fit() }) {}

        ui_text_styled(value_str, .color = text_color);
    }
}

static float slider_span(float usable_width, float fraction) {
    float width = usable_width * fraction;
    return width < 0.0f ? 0.0f : width;
}

static void slider_track(const UiWidgetState* w, SliderGeometry g, float normalized) {
    FlColor track_unfilled = w->theme.surface.elevated;
    FlColor fill_color = ui_apply_state_color(w->theme.text.primary, w->item);
    FlColor thumb_color = ui_apply_state_color(w->theme.text.primary, w->item);

    float filled_width = slider_span(g.usable_width, normalized);
    float unfilled_width = slider_span(g.usable_width, 1.0f - normalized);

    ui_layout_area_id(w->id, .sizing = { sizing_grow(200, 1000), sizing_fixed(g.thumb_size) },
                      .direction = LayoutDirection_LeftToRight, .child_alignment = ui_align(Left, Center),
                      .color = UI_HIT_TEST_FILL(1)) {
        ui_layout_area(.sizing = { sizing_fixed(filled_width), sizing_fixed(g.track_height) },
                       .radius = fl_corner_radius_left(FlRadius_Full), .color = fill_color) {}

        ui_layout_area(.sizing = { sizing_fixed(g.thumb_size), sizing_fixed(g.thumb_size) },
                       .radius = corner_radius(FlRadius_Full), .color = thumb_color) {}

        ui_layout_area(.sizing = { sizing_fixed(unfilled_width), sizing_fixed(g.track_height) },
                       .radius = fl_corner_radius_right(FlRadius_Full), .color = track_unfilled) {}
    }
}

// Shared slider body. is_int only governs how the display value is formatted: int sliders apply the format
// string to the rounded integer (see ui_slider_format_value).
static FlSignal slider_impl(FlString label, SliderModel* m, FlSliderConfig cfg, bool is_int) {
    FlUiCtx* self = g_ui_ctx;

    // Slider is custom-coloured (track fill / thumb below).
    FlItemFlags flags = FlItemFlags_clickable | FlItemFlags_draggable | FlItemFlags_focusable
                        | FlItemFlags_scroll_target | FlItemFlags_hot_animation | FlItemFlags_active_animation
                        | FlItemFlags_focus_animation;
    UiWidgetState w = ui_widget_begin(label, cfg.widget, flags);
    SliderGeometry geometry = slider_geometry(cfg, w.item);

    slider_apply_pointer(m, w.signal, w.item, geometry);
    slider_apply_wheel(m, w.signal);
    if (w.signal.focused) {
        slider_apply_focused_input(m, self, cfg.vertical);
    }
    slider_model_commit(m);

    FlString value_str = ui_slider_format_value(self->render_frame_allocator, cfg.format, m->value, is_int);
    FlColor text_color = cfg.widget.text_color.a > 0 ? cfg.widget.text_color : w.theme.text.primary;

    ui_layout_area(.sizing = { sizing_grow(0), sizing_fixed(FlWidgetHeight_LG) },
                   .direction = LayoutDirection_TopToBottom, .child_gap = FlSpacing_SM) {
        slider_header(label, value_str, text_color);
        slider_track(&w, geometry, m->normalized);
    }

    return w.signal;
}

FlString ui_slider_format_value(FlArena* arena, const char* format, float value, bool is_int) {
    if (is_int) {
        // Apply the format to the rounded integer; a nullptr format falls back to a bare integer.
        return fl_sprintf_arena(arena, format ? format : "%d", (int)roundf(value));
    }
    return fl_sprintf_arena(arena, format ? format : "%.2f", value);
}

FlSignal ui_slider_float_impl(FlString label, float* value, float min, float max, FlSliderConfig cfg) {
    SliderModel m = slider_model_new(*value, min, max, cfg);
    FlSignal signal = slider_impl(label, &m, cfg, false);
    *value = m.value;
    return signal;
}

FlSignal ui_slider_int_impl(FlString label, int* value, int min, int max, FlSliderConfig cfg) {
    // Drive the shared slider over a float mirror of the int, formatting the display against the integer value
    // (is_int = true).
    SliderModel m = slider_model_new((float)(*value), (float)min, (float)max, cfg);
    FlSignal signal = slider_impl(label, &m, cfg, true);
    *value = clamp((int)roundf(m.value), min, max);
    return signal;
}
