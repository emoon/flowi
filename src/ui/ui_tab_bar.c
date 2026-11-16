// Tab bar widget.

#include "ui.h"
#include "ui_private.h"
#include "ui_widget_internal.h"
#include "ui_variant_helpers.h"
#include "ui_tokens.h"
#include "layout.h"
#include <core/core.h>
#include <core/math.h>
#include <core/log.h>
#include <external/clay/clay.h>

// Horizontal or vertical tab navigation. ui_tab_strip is the embeddable core (also driven by the dock leaf
// header); ui_tab_bar_impl wraps it with the int*-storage + keyboard-nav surface.

static FlLayoutId tab_layout_id(const UiTabStrip* strip, int i) {
    return (FlLayoutId) { .name = strip->name, .id = ui_item_id_seed(strip->instance_id, (u32)i) };
}

// A caller that wants the pull-out gesture (the dock leaf header) opts the tabs into draggable +
// FlItem_DragThreshold, so a press still selects and only a press that travels reads as a drag.
static FlItemFlags tab_item_flags(const UiTabStrip* strip) {
    return strip->out_dragged != nullptr ? (STANDARD_WIDGET_FLAGS | FlItemFlags_draggable | FlItem_DragThreshold)
                                         : STANDARD_WIDGET_FLAGS;
}

// Behavior pass: resolve the selection from this frame's input before any tab lays out; tabs activate on
// press (signal.pressed). Hover state reflects last frame's geometry. ui_item_behavior runs once per tab here;
// the render pass reads cached state via get_item_state.
static int resolve_tab_selection(const UiTabStrip* strip) {
    int result = (strip->selected < 0 || strip->selected >= strip->count) ? 0 : strip->selected;
    if (strip->out_dragged != nullptr) {
        *strip->out_dragged = -1;
    }
    const FlItemFlags tab_flags = tab_item_flags(strip);

    for_count(i, strip->count) {
        FlWidgetConfig tab_cfg = {
            .focusable = strip->cfg.widget.focusable,
            .tab_index = strip->cfg.widget.tab_index + i,
        };
        FlSignal signal = ui_item_behavior(tab_layout_id(strip, i), tab_flags, tab_cfg);
        if (signal.pressed || signal.activated) {
            result = i;
        }
        if (signal.dragging && strip->out_dragged != nullptr) {
            *strip->out_dragged = i;
        }
        // The selected tab gets the hand too; in a dock header its press begins the pull-out drag.
        if (signal.hovered) {
            fl_cursor_set((FlCursor) { .shape = FlCursorShape_Hand });
        }
    }
    return result;
}

// Geometry knobs, each 0 = the built-in default. Either item padding switches a tab from the fixed
// FlWidgetHeight_MD height to padding-driven sizing.
typedef struct TabGeometry {
    bool padding_driven;
    FlLayoutPadding item_padding;
    float item_radius;
} TabGeometry;

static TabGeometry tab_geometry(const FlTabBarConfig* cfg) {
    return (TabGeometry) {
        .padding_driven = cfg->item_padding_h > 0.0f || cfg->item_padding_v > 0.0f,
        .item_padding = fl_padding_hv(cfg->item_padding_h > 0.0f ? (u16)cfg->item_padding_h : (u16)FlSpacing_MD,
                                      cfg->item_padding_v > 0.0f ? (u16)cfg->item_padding_v : (u16)FlSpacing_SM),
        .item_radius = cfg->item_radius > 0.0f ? cfg->item_radius : (float)FlRadius_MD,
    };
}

// Same 0 = built-in default as the knobs above; a negative value asks for no gap.
static u16 strip_child_gap(float child_gap) {
    if (child_gap > 0.0f) {
        return (u16)child_gap;
    }
    return child_gap < 0.0f ? (u16)0 : (u16)FlSpacing_XS;
}

// Tab sizing: fit-to-label inside the caller's [min, max] band, or, under stretch, an even
// share of the strip, floored at min_width. Clay reads a 0 max as "no cap".
static FlLayoutSizing tab_sizing(const UiTabStrip* strip, const TabGeometry* geometry) {
    const UiTabStripLayout* layout = &strip->layout;
    const f32 height = layout->height > 0.0f ? layout->height : (f32)FlWidgetHeight_MD;
    FlLayoutSizingAxis tab_height = geometry->padding_driven ? sizing_fit(0) : sizing_fixed(height);
    FlLayoutSizingAxis tab_main
        = layout->stretch ? sizing_grow(layout->min_width) : sizing_fit(layout->min_width, layout->max_width);
    return strip->cfg.vertical ? (FlLayoutSizing) { sizing_grow(0), tab_height }
                               : (FlLayoutSizing) { tab_main, tab_height };
}

// A stretched tab is a bar, so its label sits at the leading edge; a fitted one is a chip around
// its label, centred.
static FlLayoutChildAlignment tab_alignment(bool stretch) {
    return stretch ? (FlLayoutChildAlignment) { LayoutAlignmentX_Left, LayoutAlignmentY_Center }
                   : (FlLayoutChildAlignment) { LayoutAlignmentX_Center, LayoutAlignmentY_Center };
}

// The layout's variant for the selected tab (Solid, the zero, is the accent fill under a
// text.primary label), Ghost for the rest, with hover and press derived perceptually and the focus
// ring folded into the border. cfg.widget.color is not forwarded as the background override;
// cfg.widget.text_color is forwarded and spans both states.
static FlWidgetColors tab_colors(const UiTabStrip* strip, ItemState* item, bool is_selected) {
    FlWidgetVariant variant = is_selected ? strip->layout.selected_variant : FlWidgetVariant_Ghost;
    return fl_resolve_widget_colors(variant, (FlColor) { 0 }, strip->cfg.widget.text_color, item, strip->theme,
                                    /*animate_text=*/true);
}

static void emit_tab(const UiTabStrip* strip, const TabGeometry* geometry, int i, bool is_selected) {
    // Same per-instance id as the behavior pass; selection is already resolved, so this pass only draws.
    FlLayoutId tab_id = tab_layout_id(strip, i);
    FlWidgetColors colors = tab_colors(strip, get_item_state(tab_id), is_selected);
    FlColor tab_bg = ui_hit_testable(colors.bg, 2);

    // clang-format off
    ui_layout_area_id(tab_id,
        .sizing = tab_sizing(strip, geometry),
        .padding = geometry->item_padding,
        .radius = corner_radius(geometry->item_radius),
        .child_alignment = tab_alignment(strip->layout.stretch),
        .color = tab_bg,
        .border = fl_border(colors.border, FlBorderWidth_Medium))
    {
        // Elide needs a width cap; a stretched tab has one in its share of the strip.
        const bool elide = strip->layout.max_width > 0.0f || strip->layout.stretch;
        ui_text_styled_impl(strip->labels[i],
                            (FlTextConfig){ .color = colors.text,
                                            .font_size = strip->cfg.font_size,
                                            .letter_spacing = strip->cfg.letter_spacing,
                                            .valign = FlTextVAlign_Center,
                                            .no_wrap = elide,
                                            .overflow = elide ? FlOverflow_Elide : FlOverflow_Clip });
    }
    // clang-format on
}

int ui_tab_strip(const UiTabStrip* strip) {
    const FlTabBarConfig* cfg = &strip->cfg;
    int result = resolve_tab_selection(strip);
    const TabGeometry geometry = tab_geometry(cfg);

    // The strip paints a background only when asked to hold the tabs in a pill; otherwise its color is alpha 0.
    const bool has_pill = cfg->container_padding > 0.0f || cfg->container_radius > 0.0f;
    const FlColor container_color = has_pill ? strip->theme->surface.elevated : (FlColor) { 0, 0, 0, 0 };

    // A pill wraps the tabs, so the strip hugs them along its main axis.
    const FlLayoutSizingAxis main_axis = has_pill ? sizing_fit(0) : sizing_grow(0);

    // clang-format off
    ui_layout_area(
        .sizing = cfg->vertical
            ? (FlLayoutSizing){ sizing_fit(0), main_axis }
            : (FlLayoutSizing){ main_axis, sizing_fit(0) },
        .direction = cfg->vertical ? LayoutDirection_TopToBottom : LayoutDirection_LeftToRight,
        .padding = padding_all((u16)cfg->container_padding),
        .radius = corner_radius(cfg->container_radius),
        .color = container_color,
        .child_gap = strip_child_gap(cfg->child_gap),
        .child_alignment = cfg->vertical
            ? (FlLayoutChildAlignment){ LayoutAlignmentX_Left, LayoutAlignmentY_Top }
            : (FlLayoutChildAlignment){ LayoutAlignmentX_Left, LayoutAlignmentY_Center })
    {
        for_count(i, strip->count) {
            emit_tab(strip, &geometry, i, i == result);
        }
    }
    // clang-format on

    return result;
}

static void navigate_from_tab(const UiTabStrip* strip, int i) {
    FlUiCtx* self = g_ui_ctx;
    const bool vertical = strip->cfg.vertical;
    bool* nav_prev = vertical ? &self->nav_frame.nav_up : &self->nav_frame.nav_left;
    bool* nav_next = vertical ? &self->nav_frame.nav_down : &self->nav_frame.nav_right;

    if (*nav_prev && i > 0) {
        fl_set_focus(tab_layout_id(strip, i - 1));
        *nav_prev = false;
    } else if (*nav_next && i < strip->count - 1) {
        fl_set_focus(tab_layout_id(strip, i + 1));
        *nav_next = false;
    }
}

// Keyboard navigation is gated on the same value the tabs registered focus under.
static void navigate_tabs(const UiTabStrip* strip) {
    if (strip->cfg.widget.focusable == FlFocusable_No) {
        return;
    }
    for_count(i, strip->count) {
        ItemState* item = get_item_state(tab_layout_id(strip, i));
        if (item && item->state.focused_t > 0.5f) {
            navigate_from_tab(strip, i);
            return;
        }
    }
}

bool ui_tab_bar_impl(FlString* tabs, int tab_count, int* selected_index, FlTabBarConfig cfg) {
    FlUiCtx* self = g_ui_ctx;

    if (!tabs || tab_count <= 0 || !selected_index) {
        log_error("ui_tab_bar_impl: invalid parameters");
        return false;
    }

    if (*selected_index < 0 || *selected_index >= tab_count) {
        *selected_index = 0;
    }

    FlThemeColors theme_scratch;
    const FlThemeColors* theme = ui_resolve_widget_theme(self, cfg.widget.theme_override, &theme_scratch);

    // Per-tab ids key on the selection pointer (folded per tab by ui_item_id_seed).
    UiTabStrip strip = {
        .name = FL_S("tab"),
        .instance_id = (u32)(uintptr_t)selected_index,
        .labels = tabs,
        .count = tab_count,
        .selected = *selected_index,
        .cfg = cfg,
        .theme = theme,
    };
    int new_selected = ui_tab_strip(&strip);
    bool selection_changed = (new_selected != *selected_index);
    *selected_index = new_selected;

    navigate_tabs(&strip);

    return selection_changed;
}
