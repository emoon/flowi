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

int ui_tab_strip(FlString name, u32 instance_id, const FlString* labels, int count, int selected, FlTabBarConfig cfg,
                 const FlThemeColors* theme, UiTabStripLayout layout, int* out_dragged) {
    int result = (selected < 0 || selected >= count) ? 0 : selected;
    if (out_dragged != nullptr) {
        *out_dragged = -1;
    }

    // A caller that wants the pull-out gesture (the dock leaf header) opts the tabs into draggable +
    // FlItem_DragThreshold, so a press still selects and only a press that TRAVELS reads as a drag.
    const FlItemFlags tab_flags = out_dragged != nullptr
                                      ? (STANDARD_WIDGET_FLAGS | FlItemFlags_draggable | FlItem_DragThreshold)
                                      : STANDARD_WIDGET_FLAGS;

    // Behavior pass - resolve the selection from this frame's input before any tab lays out; tabs activate on
    // press (signal.pressed). Hover state reflects last frame's geometry. ui_item_behavior runs once per tab here;
    // the render pass reads cached state via get_item_state.
    for (int i = 0; i < count; i++) {
        FlLayoutId tab_id = { .name = name, .id = ui_item_id_seed(instance_id, (u32)i) };
        FlWidgetConfig tab_cfg = {
            .focusable = cfg.widget.focusable,
            .tab_index = cfg.widget.tab_index + i,
        };
        FlSignal signal = ui_item_behavior(tab_id, tab_flags, tab_cfg);
        if (signal.pressed || signal.activated) {
            result = i;
        }
        if (signal.dragging && out_dragged != nullptr) {
            *out_dragged = i;
        }
        // The selected tab gets the hand too - in a dock header its press begins the pull-out drag.
        if (signal.hovered) {
            fl_cursor_set((FlCursor) { .shape = FlCursorShape_Hand });
        }
    }

    FlLayoutDirection direction = cfg.vertical ? LayoutDirection_TopToBottom : LayoutDirection_LeftToRight;

    // Geometry knobs, each 0 = the built-in default. Either item padding switches a tab from the fixed
    // FlWidgetHeight_MD height to padding-driven sizing.
    const bool padding_driven = cfg.item_padding_h > 0.0f || cfg.item_padding_v > 0.0f;
    const FlLayoutPadding item_padding
        = fl_padding_hv(cfg.item_padding_h > 0.0f ? (u16)cfg.item_padding_h : (u16)FlSpacing_MD,
                        cfg.item_padding_v > 0.0f ? (u16)cfg.item_padding_v : (u16)FlSpacing_SM);
    const float item_radius = cfg.item_radius > 0.0f ? cfg.item_radius : (float)FlRadius_MD;

    // Same 0 = built-in default as the knobs above; a negative value asks for no gap.
    const u16 child_gap
        = cfg.child_gap > 0.0f ? (u16)cfg.child_gap : (cfg.child_gap < 0.0f ? (u16)0 : (u16)FlSpacing_XS);

    // The strip paints a background only when asked to hold the tabs in a pill; otherwise its color is alpha 0.
    const bool has_pill = cfg.container_padding > 0.0f || cfg.container_radius > 0.0f;
    const FlColor container_color = has_pill ? theme->surface.elevated : (FlColor) { 0, 0, 0, 0 };

    // A pill wraps the tabs, so the strip hugs them along its main axis.
    const FlLayoutSizingAxis main_axis = has_pill ? sizing_fit(0) : sizing_grow(0);

    // clang-format off
    ui_layout_area(
        .sizing = cfg.vertical
            ? (FlLayoutSizing){ sizing_fit(0), main_axis }
            : (FlLayoutSizing){ main_axis, sizing_fit(0) },
        .direction = direction,
        .padding = padding_all((u16)cfg.container_padding),
        .radius = corner_radius(cfg.container_radius),
        .color = container_color,
        .child_gap = child_gap,
        .child_alignment = cfg.vertical
            ? (FlLayoutChildAlignment){ LayoutAlignmentX_Left, LayoutAlignmentY_Top }
            : (FlLayoutChildAlignment){ LayoutAlignmentX_Left, LayoutAlignmentY_Center })
    {
        for (int i = 0; i < count; i++) {
            bool is_selected = (i == result);

            // Same per-instance id as the behavior pass; selection is already resolved, so this pass only draws.
            FlLayoutId tab_id = { .name = name, .id = ui_item_id_seed(instance_id, (u32)i) };
            ItemState* item = get_item_state(tab_id);

            // The layout's variant for the selected tab (Solid, the zero, is the accent fill under a
            // text.primary label), Ghost for the rest, with hover and press derived perceptually and the focus
            // ring folded into the border. cfg.widget.color is not forwarded as the background override;
            // cfg.widget.text_color is forwarded and spans both states.
            FlWidgetVariant variant = is_selected ? layout.selected_variant : FlWidgetVariant_Ghost;
            FlWidgetColors colors = fl_resolve_widget_colors(variant, (FlColor){ 0 }, cfg.widget.text_color, item,
                                                             theme, /*animate_text=*/true);

            // Tab sizing: fit-to-label inside the caller's [min, max] band, or - under stretch - an even
            // share of the strip, floored at min_width. Clay reads a 0 max as "no cap".
            const f32 height = layout.height > 0.0f ? layout.height : (f32)FlWidgetHeight_MD;
            FlLayoutSizingAxis tab_height = padding_driven ? sizing_fit(0) : sizing_fixed(height);
            FlLayoutSizingAxis tab_main
                = layout.stretch ? sizing_grow(layout.min_width) : sizing_fit(layout.min_width, layout.max_width);
            FlLayoutSizing tab_sizing = cfg.vertical
                ? (FlLayoutSizing){ sizing_grow(0), tab_height }
                : (FlLayoutSizing){ tab_main, tab_height };

            // A stretched tab is a bar, so its label sits at the leading edge; a fitted one is a chip around
            // its label, centred.
            FlLayoutChildAlignment tab_alignment
                = layout.stretch ? (FlLayoutChildAlignment){ LayoutAlignmentX_Left, LayoutAlignmentY_Center }
                                 : (FlLayoutChildAlignment){ LayoutAlignmentX_Center, LayoutAlignmentY_Center };

            FlColor tab_bg = ui_hit_testable(colors.bg, 2);
            ui_layout_area_id(tab_id,
                .sizing = tab_sizing,
                .padding = item_padding,
                .radius = corner_radius(item_radius),
                .child_alignment = tab_alignment,
                .color = tab_bg,
                .border = fl_border(colors.border, FlBorderWidth_Medium))
            {
                // Elide needs a width cap; a stretched tab has one in its share of the strip.
                const bool elide = layout.max_width > 0.0f || layout.stretch;
                ui_text_styled_impl(labels[i],
                                    (FlTextConfig){ .color = colors.text,
                                                    .font_size = cfg.font_size,
                                                    .letter_spacing = cfg.letter_spacing,
                                                    .valign = FlTextVAlign_Center,
                                                    .no_wrap = elide,
                                                    .overflow = elide ? FlOverflow_Elide : FlOverflow_Clip });
            }
        }
    }
    // clang-format on

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
    u32 instance_id = (u32)(uintptr_t)selected_index;
    int new_selected = ui_tab_strip(FL_S("tab"), instance_id, tabs, tab_count, *selected_index, cfg, theme,
                                    (UiTabStripLayout) { 0 }, nullptr);
    bool selection_changed = (new_selected != *selected_index);
    *selected_index = new_selected;

    // Keyboard navigation is gated on the same value the tabs registered focus under.
    if (cfg.widget.focusable != FlFocusable_No) {
        for (int i = 0; i < tab_count; i++) {
            FlLayoutId tab_id = { .name = FL_S("tab"), .id = ui_item_id_seed(instance_id, (u32)i) };
            ItemState* item = get_item_state(tab_id);
            if (item && item->state.focused_t > 0.5f) {
                bool nav_prev = cfg.vertical ? self->nav_frame.nav_up : self->nav_frame.nav_left;
                bool nav_next = cfg.vertical ? self->nav_frame.nav_down : self->nav_frame.nav_right;

                if (nav_prev && i > 0) {
                    FlLayoutId prev_tab_id = { .name = FL_S("tab"), .id = ui_item_id_seed(instance_id, (u32)(i - 1)) };
                    fl_set_focus(prev_tab_id);
                    // Consume input to prevent spatial navigation
                    if (cfg.vertical) {
                        self->nav_frame.nav_up = false;
                    } else {
                        self->nav_frame.nav_left = false;
                    }
                } else if (nav_next && i < tab_count - 1) {
                    FlLayoutId next_tab_id = { .name = FL_S("tab"), .id = ui_item_id_seed(instance_id, (u32)(i + 1)) };
                    fl_set_focus(next_tab_id);
                    // Consume input to prevent spatial navigation
                    if (cfg.vertical) {
                        self->nav_frame.nav_down = false;
                    } else {
                        self->nav_frame.nav_right = false;
                    }
                }
                break;
            }
        }
    }

    return selection_changed;
}
