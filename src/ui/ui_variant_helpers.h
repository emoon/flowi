#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UI Variant Helpers
//
// Variants define the visual style of widgets:
// - Solid:   Filled accent background, high-contrast text (primary actions, selected items)
// - Outline: Border only, transparent background (secondary actions)
// - Ghost:   Transparent background, secondary text (tertiary actions, nav items)
// - Soft:    Subtle tinted background (accent at 15% opacity), accent text (tags, chips)
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <ui/ui_types.h>
#include <flowi/ui/theme.h>
#include <flowi/ui/widget_config.h> // @generated FlWidgetVariant / FlWidgetSize (api_gen)

// ItemState is internal.
struct ItemState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Base colors for a variant - before any state modifiers (hover, press, focus).

FlColor ui_get_variant_bg(FlWidgetVariant variant, const FlThemeColors* theme);

FlColor ui_get_variant_text(FlWidgetVariant variant, const FlThemeColors* theme);

FlColor ui_get_variant_border(FlWidgetVariant variant, const FlThemeColors* theme);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State Color Application
//
// Apply hover/press/focus state modifiers to base colors, driven by the ItemState animation values
// (hot_t, active_t, focused_t).

// Derive the hovered/pressed form of a base color: hot_t perceptually lightens it, then active_t perceptually
// darkens the result. Returns base unchanged when item is null or both values are 0.
FlColor ui_apply_state_color(FlColor base, const struct ItemState* item);

// Apply focus to a border color: OKLab-lerps toward theme->interactive.focus, giving the focus ring.
FlColor ui_apply_focus_border(FlColor base_border, const struct ItemState* item, const FlThemeColors* theme);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Widget Color Resolution
//
// The uniform per-widget color prologue (bg/text/border base selection + state application). Call sites pass
// overrides (or zero) and a flag for whether text receives state animation. State derivation is perceptual
// (OKLCH/OKLab).

typedef struct {
    FlColor bg, text, border;
} FlWidgetColors;

FlWidgetColors fl_resolve_widget_colors(FlWidgetVariant variant, FlColor bg_override, FlColor text_override,
                                        const struct ItemState* item, const FlThemeColors* theme, bool animate_text);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sparse theme override merge
//
// Layer a sparse FlThemeOverride over a base theme: copy only the slots flagged in override->slots, inherit
// every other slot from base. Returns the merged theme by value. The single merge primitive behind both scoped
// subtree overrides (ui_theme_push) and per-widget state-slot overrides (FlWidgetConfig::theme_override), so a
// widget override layers on top of whatever the subtree resolved to. A null override (or one with no slots
// flagged) returns a copy of base unchanged.

FlThemeColors fl_theme_merge_override(const FlThemeColors* base, const FlThemeOverride* override);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convenience Functions

static inline bool ui_color_is_transparent(FlColor c) {
    return c.a == 0;
}

static inline bool ui_color_is_zero(FlColor c) {
    return c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0;
}
