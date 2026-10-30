#include "ui_variant_helpers.h"
#include "color_oklch.h"
#include "ui_color_utils.h"
#include "ui_private.h"
#include <stddef.h> // offsetof

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constants for state color modification
//
// Hover/press/focus are derived from a base color perceptually (OKLCH lighten/darken, OKLab focus
// lerp), reading interactive.base only.

#define HOVER_LIGHTEN_AMOUNT 0.15f
#define PRESS_DARKEN_AMOUNT 0.15f
#define SOFT_BG_ALPHA 38 // 15% of 255 ~ 38

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlColor ui_get_variant_bg(FlWidgetVariant variant, const FlThemeColors* theme) {
    switch (variant) {
        case FlWidgetVariant_Solid:
            return theme->interactive.base;

        case FlWidgetVariant_Outline:
            return theme->surface.base;

        case FlWidgetVariant_Ghost:
            return theme->surface.base;

        case FlWidgetVariant_Soft:
            return (FlColor) {
                .r = theme->interactive.base.r,
                .g = theme->interactive.base.g,
                .b = theme->interactive.base.b,
                .a = SOFT_BG_ALPHA,
            };

        default:
            return theme->interactive.base;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlColor ui_get_variant_text(FlWidgetVariant variant, const FlThemeColors* theme) {
    switch (variant) {
        case FlWidgetVariant_Solid:
            return theme->text.primary;

        case FlWidgetVariant_Outline:
            return theme->text.primary;

        case FlWidgetVariant_Ghost:
            return theme->text.secondary;

        case FlWidgetVariant_Soft:
            return theme->text.accent;

        default:
            return theme->text.primary;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlColor ui_get_variant_border(FlWidgetVariant variant, const FlThemeColors* theme) {
    switch (variant) {
        case FlWidgetVariant_Solid:
            return (FlColor) { 0, 0, 0, 0 }; // No border

        case FlWidgetVariant_Outline:
            return theme->interactive.base;

        case FlWidgetVariant_Ghost:
            return (FlColor) { 0, 0, 0, 0 }; // No border

        case FlWidgetVariant_Soft:
            return (FlColor) { 0, 0, 0, 0 }; // No border

        default:
            return (FlColor) { 0, 0, 0, 0 };
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlColor ui_apply_state_color(FlColor base, const struct ItemState* item) {
    if (item == nullptr) {
        return base;
    }

    FlColor result = base;
    float hot_t = item->state.hot_t;
    float active_t = item->state.active_t;

    if (hot_t > 0.0f) {
        result = color_lighten_perceptual(result, HOVER_LIGHTEN_AMOUNT * hot_t);
    }

    // Press darkens on top of the hover lighten, cancelling part of it.
    if (active_t > 0.0f) {
        result = color_darken_perceptual(result, PRESS_DARKEN_AMOUNT * active_t);
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlColor ui_apply_focus_border(FlColor base_border, const struct ItemState* item, const FlThemeColors* theme) {
    if (item == nullptr || theme == nullptr) {
        return base_border;
    }

    float focused_t = item->state.focused_t;

    if (focused_t <= 0.0f) {
        return base_border;
    }

    FlColor focus_color = theme->interactive.focus;

    // A transparent base has no hue to lerp from, so fade the focus color in instead.
    if (ui_color_is_transparent(base_border)) {
        return ui_color_with_alpha(focus_color, (u8)(255 * focused_t));
    }

    return color_lerp_oklab(base_border, focus_color, focused_t);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlWidgetColors fl_resolve_widget_colors(FlWidgetVariant variant, FlColor bg_override, FlColor text_override,
                                        const struct ItemState* item, const FlThemeColors* theme, bool animate_text) {
    FlColor bg_base = (bg_override.a > 0) ? bg_override : ui_get_variant_bg(variant, theme);
    FlColor text_base = (text_override.a > 0) ? text_override : ui_get_variant_text(variant, theme);
    FlColor border_base = ui_get_variant_border(variant, theme);

    FlWidgetColors result;
    result.bg = ui_apply_state_color(bg_base, item);
    result.text = animate_text ? ui_apply_state_color(text_base, item) : text_base;
    result.border = ui_apply_focus_border(border_base, item, theme);
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Byte offset of each semantic slot within FlThemeColors, indexed by FlThemeSlot bit position. The order must match
// the FlThemeSlot enum so bit i selects k_theme_slot_offset[i]. A merge copies only the flagged slots, leaving every
// other slot inherited from the base theme.

static const size_t k_theme_slot_offset[] = {
    offsetof(FlThemeColors, interactive.base),     offsetof(FlThemeColors, interactive.hover),
    offsetof(FlThemeColors, interactive.press),    offsetof(FlThemeColors, interactive.focus),
    offsetof(FlThemeColors, interactive.disabled), offsetof(FlThemeColors, surface.base),
    offsetof(FlThemeColors, surface.elevated),     offsetof(FlThemeColors, surface.overlay),
    offsetof(FlThemeColors, surface.selected),     offsetof(FlThemeColors, border.default_color),
    offsetof(FlThemeColors, border.focus),         offsetof(FlThemeColors, border.separator),
    offsetof(FlThemeColors, text.primary),         offsetof(FlThemeColors, text.secondary),
    offsetof(FlThemeColors, text.accent),          offsetof(FlThemeColors, feedback.success),
    offsetof(FlThemeColors, feedback.warning),     offsetof(FlThemeColors, feedback.error),
    offsetof(FlThemeColors, feedback.info),
};

#define FL_THEME_SLOT_COUNT ((int)(sizeof(k_theme_slot_offset) / sizeof(k_theme_slot_offset[0])))

FlThemeColors fl_theme_merge_override(const FlThemeColors* base, const FlThemeOverride* override) {
    FlThemeColors merged = *base;
    if (override) {
        for (int i = 0; i < FL_THEME_SLOT_COUNT; i++) {
            if (override->slots & (1u << i)) {
                const size_t off = k_theme_slot_offset[i];
                *(FlColor*)((char*)&merged + off) = *(const FlColor*)((const char*)&override->colors + off);
            }
        }
    }
    return merged;
}
