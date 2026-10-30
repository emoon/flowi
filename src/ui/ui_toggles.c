// Toggle family: checkbox, switch, radio group, pill, chip - plus the shared toggle_widget_prepare helper
// and ToggleWidgetState.

#include "ui.h"
#include "ui_private.h"
#include "ui_widget_internal.h"
#include "ui_variant_helpers.h"
#include "ui_color_utils.h"
#include "ui_tokens.h"
#include "layout.h"
#include "animation.h"
#include "image.h"
#include <core/core.h>
#include <core/math.h>
#include <core/log.h>
#include <external/clay/clay.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
FlSignal ui_checkbox_impl(FlString label, bool* checked, FlCheckboxConfig cfg) {
    FlUiCtx* self = g_ui_ctx;

    // Checkbox is custom-coloured (it blends an animated fill between two theme slots below).
    UiWidgetState w = ui_widget_begin(label, cfg.widget, STANDARD_WIDGET_FLAGS);
    FlLayoutId checkbox_id = w.id;
    FlSignal signal = w.signal;
    ItemState* item = w.item;
    const FlThemeColors* theme = &w.theme;

    // Flip the caller's bool on click, before the animation target is read below.
    if (signal.clicked) {
        *checked = !(*checked);
    }

    // Fixed checkbox box size (24px)
    const float box_size = (float)FlSpacing_LG;

    // Checkmark fill animation, eased via the shared toggle helper (as for the switch knob and the radio dot).
    f32 check_anim_t = 0.0f;
    if (item != nullptr) {
        item->check_anim_t = anim_toggle_toward(item->check_anim_t, *checked, ANIM_SPEED_DEFAULT, self->delta_time);
        check_anim_t = item->check_anim_t;
    }

    // Unchecked: surface background, Checked: accent-filled background
    FlColor unchecked_bg = cfg.widget.color.a > 0 ? cfg.widget.color : theme->surface.elevated;
    FlColor checked_bg = theme->interactive.base;
    FlColor base_bg = ui_color_lerp(unchecked_bg, checked_bg, check_anim_t);
    FlColor text_color = cfg.widget.text_color.a > 0 ? cfg.widget.text_color : theme->text.primary;
    FlColor checkmark_color = (FlColor) { 0, 0, 0, 255 }; // Dark checkmark on accent background

    // Border transitions from default to accent when checked
    FlColor unchecked_border = theme->border.default_color;
    FlColor checked_border = theme->interactive.base;
    FlColor base_border = ui_color_lerp(unchecked_border, checked_border, check_anim_t);

    FlColor box_bg = ui_apply_state_color(base_bg, item);
    FlColor box_border = ui_apply_focus_border(base_border, item, theme);

    // The ID is on the container so clicking anywhere (box or text) toggles the checkbox.
    ui_layout_area_id(checkbox_id, .sizing = { sizing_grow(0), sizing_fixed(box_size + FlSpacing_SM) },
                      .direction = LayoutDirection_LeftToRight, .child_gap = FlSpacing_MD,
                      .child_alignment = ui_align(Left, Center), .color = UI_HIT_TEST_FILL(2)) {
        if (cfg.label_on_left && label.length > 0) {
            ui_text_styled(label, .valign = FlTextVAlign_Center, .color = text_color);
        }

        ui_layout_area(.sizing = { sizing_fixed(box_size), sizing_fixed(box_size) },
                       .radius = corner_radius(FlRadius_MD), .color = box_bg,
                       .child_alignment = ui_align(Center, Center),
                       .border = fl_border(box_border, FlBorderWidth_Medium)) {
            if (check_anim_t > 0.01f) {
                float checkmark_size = (box_size - FlSpacing_SM) * check_anim_t;
                ui_layout_area(.sizing = { sizing_fixed(checkmark_size), sizing_fixed(checkmark_size) }) {
                    ui_image(self->shared->icons[FlIcon_Checkmark], .color = checkmark_color);
                }
            }
        }

        if (!cfg.label_on_left && label.length > 0) {
            ui_text_styled(label, .valign = FlTextVAlign_Center, .color = text_color);
        }
    }

    return signal;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Switch implementation
//
// Same boolean role as the checkbox (the same click-toggle on a bool), but drawn as a pill track with a knob that
// slides from the left (off) to the right (on). The knob position is driven by a shared toggle-animation value
// (anim_toggle_toward, animation.h).

FlSignal ui_switch_impl(FlString label, bool* value, FlSwitchConfig cfg) {
    FlUiCtx* self = g_ui_ctx;

    // Switch is custom-coloured (animated track blend below).
    UiWidgetState w = ui_widget_begin(label, cfg.widget, STANDARD_WIDGET_FLAGS);
    FlLayoutId switch_id = w.id;
    FlSignal signal = w.signal;
    ItemState* item = w.item;
    const FlThemeColors* theme = &w.theme;

    // Toggle *value on click, before the knob target is read below.
    if (signal.clicked) {
        *value = !(*value);
    }

    // Track / knob geometry, each dimension config-driven with 0 meaning "keep the default": a pill twice as
    // wide as it is tall (48x24) with a circular knob inset 2px inside it. The knob size is derived from the
    // track height and the inset. The knob slides across travel as the toggle animates.
    const float track_h = cfg.track_height > 0.0f ? cfg.track_height : (float)FlSpacing_LG;
    const float track_w = cfg.track_width > 0.0f ? cfg.track_width : track_h * 2.0f;
    const float knob_inset = cfg.knob_inset > 0.0f ? cfg.knob_inset : (float)FlSpacing_XS / 2.0f;
    // Both clamps keep knob_size and travel non-negative (the u16 padding below is derived from them).
    const float knob_size = max_f32_impl(track_h - 2.0f * knob_inset, 0.0f);
    const float travel = max_f32_impl(track_w - knob_size - 2.0f * knob_inset, 0.0f);

    f32 knob_t = 0.0f;
    if (item != nullptr) {
        item->check_anim_t = anim_toggle_toward(item->check_anim_t, *value, ANIM_SPEED_DEFAULT, self->delta_time);
        knob_t = item->check_anim_t;
    }

    // Track color blends from the unchecked surface to the accent as it turns on, mirroring the checkbox.
    FlColor off_track = cfg.widget.color.a > 0 ? cfg.widget.color : theme->surface.elevated;
    FlColor on_track = theme->interactive.base;
    FlColor base_track = ui_color_lerp(off_track, on_track, knob_t);

    FlColor track_bg = ui_apply_state_color(base_track, item);

    FlColor unchecked_border = theme->border.default_color;
    FlColor checked_border = theme->interactive.base;
    FlColor base_border = ui_color_lerp(unchecked_border, checked_border, knob_t);
    FlColor track_border = ui_apply_focus_border(base_border, item, theme);

    // High-contrast knob that reads on both the off (surface) and on (accent) track.
    FlColor knob_color = theme->text.primary;
    FlColor text_color = cfg.widget.text_color.a > 0 ? cfg.widget.text_color : theme->text.primary;

    // The ID is on the container so a click anywhere toggles.
    ui_layout_area_id(switch_id, .sizing = { sizing_grow(0), sizing_fixed(track_h) },
                      .direction = LayoutDirection_LeftToRight, .child_gap = FlSpacing_MD,
                      .child_alignment = ui_align(Left, Center), .color = UI_HIT_TEST_FILL(2)) {
        if (cfg.label_on_left && label.length > 0) {
            ui_text_styled(label, .valign = FlTextVAlign_Center, .color = text_color);
        }

        // Pill track. Left padding positions the knob along its travel; top/bottom inset centers it vertically.
        // Padding is integer pixels; the animation value itself stays continuous. Clamped to the u16 padding
        // range.
        const float pad_max = 65535.0f;
        u16 knob_left = (u16)clamp_f32_impl(knob_inset + travel * knob_t, 0.0f, pad_max);
        u16 inset_pad = (u16)clamp_f32_impl(knob_inset, 0.0f, pad_max);
        ui_layout_area(.sizing = { sizing_fixed(track_w), sizing_fixed(track_h) },
                       .radius = corner_radius(FlRadius_Full), .color = track_bg,
                       .padding = { .left = knob_left, .top = inset_pad, .bottom = inset_pad },
                       .child_alignment = ui_align(Left, Center),
                       .border = fl_border(track_border, FlBorderWidth_Medium)) {
            ui_layout_area(.sizing = { sizing_fixed(knob_size), sizing_fixed(knob_size) },
                           .radius = corner_radius(FlRadius_Full), .color = knob_color) {}
        }

        if (!cfg.label_on_left && label.length > 0) {
            ui_text_styled(label, .valign = FlTextVAlign_Center, .color = text_color);
        }
    }

    return signal;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Radio group implementation
//
// Exclusive single-choice selection across options[]: each option is an outline circle + label, the selected one
// filled with an accent dot. Selecting an option writes its index back to *selected_index, which clears the rest
// (only one index can be current). The selection is resolved in a behavior pass before any option lays out, so
// the new selection draws the same frame.

FlSignal ui_radio_group_impl(FlString* options, int option_count, int* selected_index, FlRadioConfig cfg) {
    FlUiCtx* self = g_ui_ctx;

    if (!options || option_count <= 0 || !selected_index) {
        log_error("ui_radio_group_impl: invalid parameters");
        return (FlSignal) { 0 };
    }

    if (*selected_index < 0 || *selected_index >= option_count) {
        *selected_index = 0;
    }

    FlThemeColors theme_scratch;
    const FlThemeColors* theme = ui_resolve_widget_theme(self, cfg.widget.theme_override, &theme_scratch);

    // The selected_index pointer is the per-instance key; ui_item_id_seed folds it with each option index (same
    // seeding the tab strip uses).
    u32 instance_id = (u32)(uintptr_t)selected_index;

    FlSignal group_signal = { 0 };

    // Behavior pass - resolve the selection from this frame's input before any option lays out. Radio options
    // select on click/activate (release), like the checkbox, not on press. Each option's ui_item_behavior runs
    // once here; the render pass reads cached state via get_item_state.
    for (int i = 0; i < option_count; i++) {
        FlLayoutId opt_id = { .name = FL_S("radio"), .id = ui_item_id_seed(instance_id, (u32)i) };
        FlWidgetConfig opt_cfg = {
            .focusable = cfg.widget.focusable,
            .tab_index = cfg.widget.tab_index + i,
        };
        FlSignal signal = ui_item_behavior(opt_id, STANDARD_WIDGET_FLAGS, opt_cfg);
        if ((signal.clicked || signal.activated) && *selected_index != i) {
            *selected_index = i;
            group_signal.clicked = true; // selection changed this frame
        }
        group_signal.hovered |= signal.hovered;
        group_signal.focused |= signal.focused;
    }

    // Geometry: an outline circle with an accent dot inside, sized to read alongside the checkbox and switch.
    const float circle_size = (float)FlSpacing_LG;           // 24
    const float dot_max = circle_size - (float)FlSpacing_SM; // dot diameter at full selection (scaled by anim)

    FlColor text_color = cfg.widget.text_color.a > 0 ? cfg.widget.text_color : theme->text.primary;
    FlColor dot_color = theme->interactive.base;

    ui_layout_area(.sizing = { sizing_grow(0), sizing_fit(0) }, .direction = LayoutDirection_TopToBottom,
                   .child_gap = FlSpacing_SM, .child_alignment = ui_align(Left, Center)) {
        for (int i = 0; i < option_count; i++) {
            bool is_selected = (i == *selected_index);

            // Same per-instance id as the behavior pass; selection is already resolved, so this pass only draws
            // (and supplies the hit-test rect via ui_layout_area_id).
            FlLayoutId opt_id = { .name = FL_S("radio"), .id = ui_item_id_seed(instance_id, (u32)i) };
            ItemState* item = get_item_state(opt_id);

            f32 dot_t = 0.0f;
            if (item != nullptr) {
                item->check_anim_t
                    = anim_toggle_toward(item->check_anim_t, is_selected, ANIM_SPEED_DEFAULT, self->delta_time);
                dot_t = item->check_anim_t;
            }

            // Border transitions from default to accent as the option fills in, mirroring the checkbox / switch.
            FlColor base_border = ui_color_lerp(theme->border.default_color, theme->interactive.base, dot_t);
            FlColor circle_border = ui_apply_focus_border(base_border, item, theme);
            FlColor circle_bg = ui_apply_state_color(theme->surface.elevated, item);

            // The ID is on the row so clicking the circle or the label selects.
            ui_layout_area_id(opt_id, .sizing = { sizing_grow(0), sizing_fixed(circle_size + FlSpacing_SM) },
                              .direction = LayoutDirection_LeftToRight, .child_gap = FlSpacing_MD,
                              .child_alignment = ui_align(Left, Center), .color = UI_HIT_TEST_FILL(2)) {
                ui_layout_area(.sizing = { sizing_fixed(circle_size), sizing_fixed(circle_size) },
                               .radius = corner_radius(FlRadius_Full), .color = circle_bg,
                               .child_alignment = ui_align(Center, Center),
                               .border = fl_border(circle_border, FlBorderWidth_Medium)) {
                    if (dot_t > 0.01f) {
                        float dot_size = dot_max * dot_t;
                        ui_layout_area(.sizing = { sizing_fixed(dot_size), sizing_fixed(dot_size) },
                                       .radius = corner_radius(FlRadius_Full), .color = dot_color) {}
                    }
                }

                if (options[i].length > 0) {
                    ui_text_styled(options[i], .valign = FlTextVAlign_Center, .color = text_color);
                }
            }
        }
    }

    return group_signal;
}

// Toggle widget helper - shared logic for pill and chip widgets

typedef struct ToggleWidgetState {
    FlLayoutId id;
    FlSignal signal;
    FlColor bg_color;
    FlColor text_color;
    FlColor border_color;
    bool valid; // false if validation failed
} ToggleWidgetState;

// bg_alpha_boost: multiplier for background alpha when selected (1.0 = no change)
static ToggleWidgetState toggle_widget_prepare(FlString label, bool* selected, FlWidgetConfig cfg,
                                               FlWidgetVariant variant, f32 bg_alpha_boost, const char* func_name) {
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

// Pill widget - pill-shaped toggle button for category filters

// clang-format off
FlSignal ui_pill_impl(FlString label, bool* selected, FlWidgetConfig cfg) {
    FlWidgetVariant variant = (selected && *selected) ? FlWidgetVariant_Solid : FlWidgetVariant_Ghost;
    ToggleWidgetState s = toggle_widget_prepare(label, selected, cfg, variant, 1.0f, "ui_pill_impl");

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

// Chip widget - small tag/badge with soft variant

static const f32 CHIP_SELECTED_ALPHA_BOOST = 1.5f;

// clang-format off
FlSignal ui_chip_impl(FlString label, bool* selected, FlWidgetConfig cfg) {
    f32 alpha_boost = (selected && *selected) ? CHIP_SELECTED_ALPHA_BOOST : 1.0f;
    ToggleWidgetState s = toggle_widget_prepare(label, selected, cfg, FlWidgetVariant_Soft, alpha_boost, "ui_chip_impl");

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
