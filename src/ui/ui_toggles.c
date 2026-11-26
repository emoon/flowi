// Toggle family: checkbox, switch, radio group, pill, chip, plus the shared toggle_widget_prepare helper
// and ToggleWidgetState.

#include "ui.h"
#include "ui_private.h"
#include "ui_widget_internal.h"
#include "ui_variant_helpers.h"
#include "ui_color_utils.h"
#include "ui_tokens.h"
#include "layout.h"
#include "animation.h"
#include "image/image.h"
#include <core/core.h>
#include <core/math.h>
#include <core/log.h>
#include <external/clay/clay.h>

static f32 toggle_anim_value(ItemState* item, bool target) {
    const f32 target_t = target ? 1.0f : 0.0f;
    if (item == nullptr) {
        return target_t;
    }
    if (!item->check_anim_initialized) {
        item->check_anim_initialized = true;
        item->check_anim_t = target_t;
        return target_t;
    }
    item->check_anim_t = anim_toggle_toward(item->check_anim_t, target, ANIM_SPEED_DEFAULT, g_ui_ctx->delta_time);
    return item->check_anim_t;
}

static FlColor toggle_color_or(FlColor color, FlColor fallback) {
    return color.a > 0 ? color : fallback;
}

static FlColor toggle_border_color(const FlThemeColors* theme, ItemState* item, f32 t) {
    FlColor base_border = ui_color_lerp(theme->border.default_color, theme->interactive.base, t);
    return ui_apply_focus_border(base_border, item, theme);
}

static void toggle_label(FlString label, bool show, FlColor color) {
    if (show && label.length > 0) {
        ui_text_styled(label, .valign = FlTextVAlign_Center, .color = color);
    }
}

static void toggle_flip_on_click(FlSignal signal, bool* value) {
    if (signal.clicked) {
        *value = !(*value);
    }
}

static void checkbox_draw_mark(f32 check_anim_t, float box_size) {
    if (check_anim_t <= 0.01f) {
        return;
    }
    FlColor checkmark_color = (FlColor) { 0, 0, 0, 255 }; // Dark checkmark on accent background
    float checkmark_size = (box_size - FlSpacing_SM) * check_anim_t;
    ui_layout_area(.sizing = { sizing_fixed(checkmark_size), sizing_fixed(checkmark_size) }) {
        ui_image(g_ui_ctx->shared->icons[FlIcon_Checkmark], .color = checkmark_color);
    }
}

FlSignal ui_checkbox_impl(FlString label, bool* checked, FlCheckboxConfig cfg) {
    // Checkbox is custom-coloured (it blends an animated fill between two theme slots below).
    UiWidgetState w = ui_widget_begin(label, cfg.widget, STANDARD_WIDGET_FLAGS);
    FlLayoutId checkbox_id = w.id;
    FlSignal signal = w.signal;
    ItemState* item = w.item;
    const FlThemeColors* theme = &w.theme;

    // Flip the caller's bool on click, before the animation target is read below.
    toggle_flip_on_click(signal, checked);

    // Fixed checkbox box size (24px)
    const float box_size = (float)FlSpacing_LG;

    // Checkmark fill animation, eased via the shared toggle helper (as for the switch knob and the radio dot).
    f32 check_anim_t = toggle_anim_value(item, *checked);

    // Unchecked: surface background, Checked: accent-filled background
    FlColor unchecked_bg = toggle_color_or(cfg.widget.color, theme->surface.elevated);
    FlColor base_bg = ui_color_lerp(unchecked_bg, theme->interactive.base, check_anim_t);
    FlColor text_color = toggle_color_or(cfg.widget.text_color, theme->text.primary);

    FlColor box_bg = ui_apply_state_color(base_bg, item);
    FlColor box_border = toggle_border_color(theme, item, check_anim_t);

    // The ID is on the container so clicking anywhere (box or text) toggles the checkbox.
    ui_layout_area_id(checkbox_id, .sizing = { sizing_grow(0), sizing_fixed(box_size + FlSpacing_SM) },
                      .direction = LayoutDirection_LeftToRight, .child_gap = FlSpacing_MD,
                      .child_alignment = ui_align(Left, Center), .color = UI_HIT_TEST_FILL(2)) {
        toggle_label(label, cfg.label_on_left, text_color);

        ui_layout_area(.sizing = { sizing_fixed(box_size), sizing_fixed(box_size) },
                       .radius = corner_radius(FlRadius_MD), .color = box_bg,
                       .child_alignment = ui_align(Center, Center),
                       .border = fl_border(box_border, FlBorderWidth_Medium)) {
            checkbox_draw_mark(check_anim_t, box_size);
        }

        toggle_label(label, !cfg.label_on_left, text_color);
    }

    return signal;
}

// Switch
//
// Same boolean role as the checkbox (the same click-toggle on a bool), but drawn as a pill track with a knob that
// slides from the left (off) to the right (on). The knob position is driven by a shared toggle-animation value
// (anim_toggle_toward, animation.h).

typedef struct SwitchLook {
    float track_h;
    float track_w;
    float knob_inset;
    float knob_size;
    float travel;
    f32 knob_t;
    FlColor track_bg;
    FlColor track_border;
    FlColor knob_color;
} SwitchLook;

static float positive_or(float value, float fallback) {
    return value > 0.0f ? value : fallback;
}

// Track / knob geometry, each dimension config-driven with 0 meaning "keep the default": a pill twice as
// wide as it is tall (48x24) with a circular knob inset 2px inside it. The knob size is derived from the
// track height and the inset. The knob slides across travel as the toggle animates.
static SwitchLook switch_geometry(const FlSwitchConfig* cfg) {
    SwitchLook look = { 0 };
    look.track_h = positive_or(cfg->track_height, (float)FlSpacing_LG);
    look.track_w = positive_or(cfg->track_width, look.track_h * 2.0f);
    look.knob_inset = positive_or(cfg->knob_inset, (float)FlSpacing_XS / 2.0f);
    // Both clamps keep knob_size and travel non-negative (the u16 padding below is derived from them).
    look.knob_size = max(look.track_h - 2.0f * look.knob_inset, 0.0f);
    look.travel = max(look.track_w - look.knob_size - 2.0f * look.knob_inset, 0.0f);
    return look;
}

// Pill track. Left padding positions the knob along its travel; top/bottom inset centers it vertically.
// Padding is integer pixels; the animation value itself stays continuous. Clamped to the u16 padding range.
static void switch_draw_track(const SwitchLook* look) {
    const float pad_max = 65535.0f;
    u16 knob_left = (u16)clamp(look->knob_inset + look->travel * look->knob_t, 0.0f, pad_max);
    u16 inset_pad = (u16)clamp(look->knob_inset, 0.0f, pad_max);
    ui_layout_area(.sizing = { sizing_fixed(look->track_w), sizing_fixed(look->track_h) },
                   .radius = corner_radius(FlRadius_Full), .color = look->track_bg,
                   .padding = { .left = knob_left, .top = inset_pad, .bottom = inset_pad },
                   .child_alignment = ui_align(Left, Center),
                   .border = fl_border(look->track_border, FlBorderWidth_Medium)) {
        ui_layout_area(.sizing = { sizing_fixed(look->knob_size), sizing_fixed(look->knob_size) },
                       .radius = corner_radius(FlRadius_Full), .color = look->knob_color) {}
    }
}

FlSignal ui_switch_impl(FlString label, bool* value, FlSwitchConfig cfg) {
    // Switch is custom-coloured (animated track blend below).
    UiWidgetState w = ui_widget_begin(label, cfg.widget, STANDARD_WIDGET_FLAGS);
    FlLayoutId switch_id = w.id;
    FlSignal signal = w.signal;
    ItemState* item = w.item;
    const FlThemeColors* theme = &w.theme;

    // Toggle *value on click, before the knob target is read below.
    toggle_flip_on_click(signal, value);

    SwitchLook look = switch_geometry(&cfg);
    look.knob_t = toggle_anim_value(item, *value);

    // Track color blends from the unchecked surface to the accent as it turns on, mirroring the checkbox.
    FlColor off_track = toggle_color_or(cfg.widget.color, theme->surface.elevated);
    FlColor base_track = ui_color_lerp(off_track, theme->interactive.base, look.knob_t);
    look.track_bg = ui_apply_state_color(base_track, item);
    look.track_border = toggle_border_color(theme, item, look.knob_t);

    // High-contrast knob that reads on both the off (surface) and on (accent) track.
    look.knob_color = theme->text.primary;
    FlColor text_color = toggle_color_or(cfg.widget.text_color, theme->text.primary);

    // The ID is on the container so a click anywhere toggles.
    ui_layout_area_id(switch_id, .sizing = { sizing_grow(0), sizing_fixed(look.track_h) },
                      .direction = LayoutDirection_LeftToRight, .child_gap = FlSpacing_MD,
                      .child_alignment = ui_align(Left, Center), .color = UI_HIT_TEST_FILL(2)) {
        toggle_label(label, cfg.label_on_left, text_color);
        switch_draw_track(&look);
        toggle_label(label, !cfg.label_on_left, text_color);
    }

    return signal;
}

// Radio group
//
// Exclusive single-choice selection across options[]: each option is an outline circle + label, the selected one
// filled with an accent dot. Selecting an option writes its index back to *selected_index, which clears the rest
// (only one index can be current). The selection is resolved in a behavior pass before any option lays out, so
// the new selection draws the same frame.

typedef struct RadioStyle {
    const FlThemeColors* theme;
    FlColor text_color;
    FlColor dot_color;
    float circle_size;
    float dot_max;
} RadioStyle;

static bool radio_args_valid(const FlString* options, int option_count, const int* selected_index) {
    return options && option_count > 0 && selected_index;
}

static bool radio_selection_changed(FlSignal signal, int current, int index) {
    return (signal.clicked || signal.activated) && current != index;
}

static FlLayoutId radio_option_id(u32 instance_id, int index) {
    return (FlLayoutId) { .name = FL_S("radio"), .id = ui_item_id_seed(instance_id, (u32)index) };
}

// Behavior pass: resolve the selection from this frame's input before any option lays out. Radio options
// select on click/activate (release), like the checkbox, not on press. Each option's ui_item_behavior runs
// once here; the render pass reads cached state via get_item_state.
static FlSignal radio_behavior_pass(u32 instance_id, int option_count, int* selected_index, FlWidgetConfig widget) {
    FlSignal group_signal = { 0 };
    for (int i = 0; i < option_count; i++) {
        FlWidgetConfig opt_cfg = {
            .focusable = widget.focusable,
            .tab_index = widget.tab_index + i,
        };
        FlSignal signal = ui_item_behavior(radio_option_id(instance_id, i), STANDARD_WIDGET_FLAGS, opt_cfg);
        if (radio_selection_changed(signal, *selected_index, i)) {
            *selected_index = i;
            group_signal.clicked = true; // selection changed this frame
        }
        group_signal.hovered |= signal.hovered;
        group_signal.focused |= signal.focused;
    }
    return group_signal;
}

static void radio_draw_dot(const RadioStyle* style, f32 dot_t) {
    if (dot_t <= 0.01f) {
        return;
    }
    float dot_size = style->dot_max * dot_t;
    ui_layout_area(.sizing = { sizing_fixed(dot_size), sizing_fixed(dot_size) },
                   .radius = corner_radius(FlRadius_Full), .color = style->dot_color) {}
}

// Same per-instance id as the behavior pass; selection is already resolved, so this pass only draws
// (and supplies the hit-test rect via ui_layout_area_id).
static void radio_draw_option(const RadioStyle* style, FlLayoutId opt_id, FlString text, bool is_selected) {
    const FlThemeColors* theme = style->theme;
    const float circle_size = style->circle_size;
    ItemState* item = get_item_state(opt_id);
    f32 dot_t = toggle_anim_value(item, is_selected);

    // Border transitions from default to accent as the option fills in, mirroring the checkbox / switch.
    FlColor circle_border = toggle_border_color(theme, item, dot_t);
    FlColor circle_bg = ui_apply_state_color(theme->surface.elevated, item);

    // The ID is on the row so clicking the circle or the label selects.
    ui_layout_area_id(opt_id, .sizing = { sizing_grow(0), sizing_fixed(circle_size + FlSpacing_SM) },
                      .direction = LayoutDirection_LeftToRight, .child_gap = FlSpacing_MD,
                      .child_alignment = ui_align(Left, Center), .color = UI_HIT_TEST_FILL(2)) {
        ui_layout_area(.sizing = { sizing_fixed(circle_size), sizing_fixed(circle_size) },
                       .radius = corner_radius(FlRadius_Full), .color = circle_bg,
                       .child_alignment = ui_align(Center, Center),
                       .border = fl_border(circle_border, FlBorderWidth_Medium)) {
            radio_draw_dot(style, dot_t);
        }

        toggle_label(text, true, style->text_color);
    }
}

FlSignal ui_radio_group_impl(FlString* options, int option_count, int* selected_index, FlRadioConfig cfg) {
    if (!radio_args_valid(options, option_count, selected_index)) {
        log_error("ui_radio_group_impl: invalid parameters");
        return (FlSignal) { 0 };
    }

    if (*selected_index < 0 || *selected_index >= option_count) {
        *selected_index = 0;
    }

    FlThemeColors theme_scratch;
    const FlThemeColors* theme = ui_resolve_widget_theme(g_ui_ctx, cfg.widget.theme_override, &theme_scratch);

    // The selected_index pointer is the per-instance key; ui_item_id_seed folds it with each option index (same
    // seeding the tab strip uses).
    u32 instance_id = (u32)(uintptr_t)selected_index;

    FlSignal group_signal = radio_behavior_pass(instance_id, option_count, selected_index, cfg.widget);

    // Geometry: an outline circle with an accent dot inside, sized to read alongside the checkbox and switch.
    RadioStyle style = {
        .theme = theme,
        .text_color = toggle_color_or(cfg.widget.text_color, theme->text.primary),
        .dot_color = theme->interactive.base,
        .circle_size = (float)FlSpacing_LG,                         // 24
        .dot_max = (float)FlSpacing_LG - (float)FlSpacing_SM, // dot diameter at full selection (scaled by anim)
    };

    ui_layout_area(.sizing = { sizing_grow(0), sizing_fit(0) }, .direction = LayoutDirection_TopToBottom,
                   .child_gap = FlSpacing_SM, .child_alignment = ui_align(Left, Center)) {
        for (int i = 0; i < option_count; i++) {
            radio_draw_option(&style, radio_option_id(instance_id, i), options[i], i == *selected_index);
        }
    }

    return group_signal;
}

// Shared logic for the pill and chip widgets.

typedef struct ToggleWidgetState {
    FlLayoutId id;
    FlSignal signal;
    FlColor bg_color;
    FlColor text_color;
    FlColor border_color;
    bool valid; // false if validation failed
} ToggleWidgetState;

typedef struct ToggleWidgetParams {
    FlString label;
    bool* selected;
    FlWidgetConfig cfg;
    FlWidgetVariant variant;
    f32 bg_alpha_boost; // multiplier for background alpha when selected (1.0 = no change)
    const char* func_name;
} ToggleWidgetParams;

static ToggleWidgetState toggle_widget_prepare(const ToggleWidgetParams* params) {
    FlString label = params->label;
    bool* selected = params->selected;
    FlWidgetConfig cfg = params->cfg;
    FlWidgetVariant variant = params->variant;
    f32 bg_alpha_boost = params->bg_alpha_boost;
    const char* func_name = params->func_name;
    ToggleWidgetState state = { 0 };

    if (!selected) {
        log_error("%s: selected pointer is null", func_name);
        return state;
    }

    state.valid = true;

    // The toggle widgets are custom-coloured: they recompute bg/border below from the variant bg override +
    // surface.elevated border.
    UiWidgetState w = ui_widget_begin(label, cfg, STANDARD_WIDGET_FLAGS);
    state.id = w.id;
    state.signal = w.signal;

    // Flip the caller's bool on click; the alpha boost below reads the new value.
    if (w.signal.clicked) {
        *selected = !(*selected);
    }

    ItemState* item = w.item;
    const FlThemeColors* theme = &w.theme;

    FlColor bg_override = ui_get_variant_bg(variant, theme);

    // Boost the variant bg alpha, then pass it as the override (a>0 forces its use in the helper).
    if (*selected && bg_alpha_boost > 1.0f) {
        f32 new_alpha = (f32)bg_override.a * bg_alpha_boost;
        bg_override.a = (u8)(new_alpha > 255.0f ? 255.0f : new_alpha);
    }

    FlWidgetColors wc
        = fl_resolve_widget_colors(variant, bg_override, (FlColor) { 0 }, item, theme, /*animate_text=*/true);
    state.bg_color = wc.bg;
    state.text_color = wc.text;
    // The border base is surface.elevated, not the variant border.
    state.border_color = ui_apply_focus_border(theme->surface.elevated, item, theme);

    return state;
}

// clang-format off
FlSignal ui_pill_impl(FlString label, bool* selected, FlWidgetConfig cfg) {
    FlWidgetVariant variant = (selected && *selected) ? FlWidgetVariant_Solid : FlWidgetVariant_Ghost;
    ToggleWidgetState s = toggle_widget_prepare(&(ToggleWidgetParams) {
        .label = label, .selected = selected, .cfg = cfg, .variant = variant,
        .bg_alpha_boost = 1.0f, .func_name = "ui_pill_impl" });

    if (!s.valid) {
        return (FlSignal) { 0 };
    }

    ui_layout_area_id(s.id, .sizing = { sizing_fit(), sizing_fixed(FlWidgetHeight_SM) },
                      .padding = fl_padding_hv(FlSpacing_MD, FlSpacing_SM),
                      .radius = corner_radius(FlRadius_Full),
                      .child_alignment = ui_align(Center, Center),
                      .color = s.bg_color,
                      .border = fl_border(s.border_color, FlBorderWidth_Medium)) {
        ui_text_styled_impl(label, (FlTextConfig) { .color = s.text_color, .valign = FlTextVAlign_Center });
    }

    return s.signal;
}
// clang-format on

static const f32 CHIP_SELECTED_ALPHA_BOOST = 1.5f;

// clang-format off
FlSignal ui_chip_impl(FlString label, bool* selected, FlWidgetConfig cfg) {
    f32 alpha_boost = (selected && *selected) ? CHIP_SELECTED_ALPHA_BOOST : 1.0f;
    ToggleWidgetState s = toggle_widget_prepare(&(ToggleWidgetParams) {
        .label = label, .selected = selected, .cfg = cfg, .variant = FlWidgetVariant_Soft,
        .bg_alpha_boost = alpha_boost, .func_name = "ui_chip_impl" });

    if (!s.valid) {
        return (FlSignal) { 0 };
    }

    ui_layout_area_id(s.id, .sizing = { sizing_fit(), sizing_fit() },
                      .padding = fl_padding_hv(FlSpacing_SM, FlSpacing_XS),
                      .radius = corner_radius(FlRadius_Full),
                      .child_alignment = ui_align(Center, Center),
                      .color = s.bg_color,
                      .border = fl_border(s.border_color, FlBorderWidth_Thin)) {
        ui_text_styled_impl(label, (FlTextConfig) { .color = s.text_color, .valign = FlTextVAlign_Center });
    }

    return s.signal;
}
// clang-format on
