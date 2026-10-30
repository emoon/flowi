// Dropdown widget.
//
// The open popup is built on the virtualized List engine (ui_list, declarative face). The dropdown reinterprets
// the List's single-select output as a one-click commit: a per-frame selection-change (a mouse click) or
// activated_id (double-click/Enter) → write *selected_index (index = id - 1) and close the popup.

#include "ui.h"
#include "ui_private.h"
#include "ui_widget_internal.h"
#include "ui_popup.h"
#include "ui_text_input.h"
#include "ui_list.h"
#include <flowi/ui/format.h>
#include "ui_variant_helpers.h"
#include "ui_tokens.h"
#include "layout.h"
#include "image.h"
#include <core/core.h>
#include <core/math.h>
#include <core/log.h>
#include <external/clay/clay.h>

// Small icon size (the chevron indicator).
static const int UI_ICON_SIZE_SM = 16;

// Popup option-row metrics.
static const float UI_DROPDOWN_ROW_HEIGHT = 28.0f;
static const float UI_DROPDOWN_ROW_GAP = 2.0f;
// Popup viewport row cap when the caller leaves DropdownConfig.max_visible_rows at 0.
static const int UI_DROPDOWN_DEFAULT_MAX_ROWS = 10;

void ui_dropdown_impl(FlString label, bool* is_open, FlString* options, int option_count, int* selected_index,
                      FlDropdownConfig cfg) {
    FlUiCtx* self = g_ui_ctx;

    if (!is_open || !options || option_count <= 0 || !selected_index) {
        log_error("ui_dropdown_impl: invalid parameters");
        return;
    }

    if (*selected_index < 0 || *selected_index >= option_count) {
        *selected_index = 0;
    }

    // The button id must match the popup anchor.
    FlLayoutId button_id = resolve_widget_id(label, cfg.widget.id_override);

    FlString current_option = options[*selected_index];
    FlString button_text;
    if (label.length > 0) {
        button_text = fl_format_frame_string("%S: %S", label, current_option);
    } else {
        button_text = current_option;
    }

    // The trigger is an ordinary single-pass variant-coloured button (widget prologue).
    FlWidgetVariant variant = FlWidgetVariant_Outline;
    FlWidgetConfig button_cfg = {
        .id_override = button_id.name,
        .theme_override = cfg.widget.theme_override,
        .color = cfg.widget.color,
        .text_color = cfg.widget.text_color,
        .focusable = cfg.widget.focusable,
        .auto_focus = cfg.widget.auto_focus,
        .tab_index = cfg.widget.tab_index,
        .variant = variant,
    };
    UiWidgetState w = ui_widget_prepare(label, button_cfg, STANDARD_WIDGET_FLAGS, variant, /*animate_text=*/false);
    FlSignal button_signal = w.signal;
    const FlThemeColors* theme = &w.theme;
    FlColor bg_color = w.colors.bg;
    FlColor text_color = w.colors.text;
    FlColor border_color = w.colors.border;

    FlColor chevron_color = theme->text.primary;

    // Trigger geometry; 0 in a knob means the built-in default. A non-zero padding_v switches the trigger from the
    // fixed FlWidgetHeight_MD height to padding-driven height; padding_h only insets.
    const FlLayoutPadding trigger_padding
        = fl_padding_hv(cfg.padding_h > 0.0f ? (u16)cfg.padding_h : (u16)FlSpacing_MD,
                        cfg.padding_v > 0.0f ? (u16)cfg.padding_v : (u16)FlSpacing_SM);
    const float radius = cfg.corner_radius > 0.0f ? cfg.corner_radius : (float)FlRadius_MD;
    const FlLayoutSizingAxis trigger_height = cfg.padding_v > 0.0f ? sizing_fit(0) : fl_sizing_height_md();

    ui_layout_area_id(button_id, .sizing = { sizing_grow(0), trigger_height }, .direction = LayoutDirection_LeftToRight,
                      .padding = trigger_padding, .child_alignment = ui_align(Left, Center), .child_gap = FlSpacing_SM,
                      .radius = corner_radius(radius), .color = bg_color,
                      .border = fl_border(border_color, FlBorderWidth_Medium)) {
        ui_text_styled_impl(button_text, (FlTextConfig) { .color = text_color, .valign = FlTextVAlign_Center });

        // Spacer to push chevron to the right
        ui_layout_area(.sizing = { sizing_grow(0), sizing_fixed(1) }) {}

        ui_layout_area(.sizing = { sizing_fixed(UI_ICON_SIZE_SM), sizing_fixed(UI_ICON_SIZE_SM) },
                       .child_alignment = ui_align(Center, Center)) {
            ui_image(self->shared->icons[FlIcon_ChevronDown], .color = chevron_color);
        }
    }

    // Track the false->true edge so a non-searchable popup requests list focus on exactly the frame it opens.
    bool opened_this_frame = false;
    if (button_signal.clicked || button_signal.activated) {
        *is_open = !(*is_open);
        opened_this_frame = *is_open;
        // Clear search text when opening a searchable dropdown (reset the stb cursor too, not just the length).
        if (*is_open && cfg.searchable && cfg.search_state) {
            ui_text_input_clear(cfg.search_state);
        }
    }

    // popup_closed_this_frame is set by ui_popup_close_internal when auto-close triggers (click-outside, ESC).
    if (*is_open && self->popup_closed_this_frame) {
        *is_open = false;
    }

    if (*is_open) {
        ui_popup(button_id, .offset = { 0, 5 }, .z_index = UI_Z_OVERLAY, .auto_close = true, .capture_input = true) {
            float min_width = cfg.min_width > 0.0f ? cfg.min_width : 200.0f;

            FlString panel_id_str = fl_format_frame_string("%S_dropdown_panel", button_id.name);
            FlLayoutId panel_id = (FlLayoutId) { .name = panel_id_str, .id = 0 };
            FlString list_id_str = fl_format_frame_string("%S_dropdown_list", button_id.name);
            FlLayoutId list_id = (FlLayoutId) { .name = list_id_str, .id = 0 };

            ui_layout_area_id(panel_id, .sizing = { sizing_fit(min_width, 9999), sizing_fit() },
                              .direction = LayoutDirection_TopToBottom, .child_gap = 0,
                              .padding = padding_all(FlSpacing_XS), .radius = corner_radius(FlRadius_MD),
                              .color = theme->surface.base) {
                // Searchable: the search input is the first focusable widget in the popup scope, so initial focus
                // lands here on the open frame. Its visible text drives the filter below; search_focused /
                // search_clay_id feed the input<->list focus hand-off at the list site.
                bool search_focused = false;
                u32 search_clay_id = 0;
                FlString query = { 0 };
                if (cfg.searchable && cfg.search_state) {
                    FlString search_id_str = fl_format_frame_string("%S_dropdown_search", button_id.name);
                    search_clay_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = search_id_str, .id = 0 });
                    FlSignal search_signal = ui_text_input(search_id_str, cfg.search_state,
                                                           (FlTextInputConfig) {
                                                               .placeholder = FL_S("Search..."),
                                                               .min_width = min_width,
                                                               .widget = { .focusable = FlFocusable_Yes },
                                                           });
                    search_focused = search_signal.focused;
                    query = ui_text_input_get_text(cfg.search_state);
                }

                // Build the lowercased filter key from the input's VISIBLE text (null-terminated for the compare).
                char search_buf[256] = { 0 };
                u32 search_len = 0;
                if (query.length > 0) {
                    search_len
                        = query.length < sizeof(search_buf) - 1 ? (u32)query.length : (u32)(sizeof(search_buf) - 1);
                    for (u32 si = 0; si < search_len; si++) {
                        char c = query.data[si];
                        search_buf[si] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
                    }
                }

                // Build the List's item descriptors, applying the substring filter. Each row's stable id is
                // orig_index + 1: the List reserves id 0 as its "none" sentinel. Commit maps back via id - 1.
                FlListItem* items = arena_alloc_array(self->render_frame_allocator, FlListItem, option_count);
                i32 filtered_count = 0;
                for_count(i, option_count) {
                    if (search_len > 0) {
                        FlString opt = options[i];
                        if (opt.length < search_len)
                            continue;
                        bool found = false;
                        for (u64 j = 0; j <= opt.length - search_len; j++) {
                            bool eq = true;
                            for (u32 k = 0; k < search_len && eq; k++) {
                                char c = opt.data[j + k];
                                char lc = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
                                if (lc != search_buf[k])
                                    eq = false;
                            }
                            if (eq) {
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                            continue;
                    }
                    items[filtered_count++] = (FlListItem) {
                        .id = (u64)(i + 1),
                        .label = options[i],
                    };
                }

                // Transient one-element highlight set, seeded from *selected_index each open frame and never read
                // back. selection_set_create allocates a heap arena: create/destroy must pair every open frame.
                FlSelectionSet* highlight = selection_set_create(1);
                selection_set_set(highlight, (u64)(*selected_index + 1), true);

                // Bounded viewport: min(count, max_visible_rows) rows tall; the List virtualizes/scrolls the rest.
                int max_rows = cfg.max_visible_rows > 0 ? cfg.max_visible_rows : UI_DROPDOWN_DEFAULT_MAX_ROWS;
                int visible_rows = filtered_count < max_rows ? filtered_count : max_rows;
                float row_stride = UI_DROPDOWN_ROW_HEIGHT + UI_DROPDOWN_ROW_GAP;
                float viewport_h = (float)visible_rows * row_stride;

                ui_layout_area(.sizing = { sizing_grow(0), sizing_fixed(viewport_h) },
                               .direction = LayoutDirection_TopToBottom) {
                    // On the frame a NON-searchable dropdown opens, start keyboard focus in the list, seeded on
                    // the current value, so arrow keys navigate and Enter/Space commit without a first click. A
                    // searchable dropdown starts focus in its search input instead and hands off to the list.
                    if (opened_this_frame && !cfg.searchable) {
                        ui_list_request_focus(list_id, (u64)(*selected_index + 1));
                    }

                    // Searchable hand-off IN: Down while the search input holds focus moves focus into the list,
                    // seeded on the first filtered row. With no filtered rows Down is a no-op; either way the Nav-Down
                    // is consumed so it neither spatial-navigates out of the search box nor advances the list cursor.
                    if (cfg.searchable && search_focused && fl_get_nav_frame().nav_down) {
                        fl_consume_nav_down();
                        if (filtered_count > 0) {
                            ui_list_request_focus(list_id, items[0].id);
                        }
                    }

                    // An empty (all-filtered-out) list feeds NULL ids to the core, which forbids a non-NULL set, so
                    // the set is dropped on that path.
                    FlSelectionSet* list_set = filtered_count > 0 ? highlight : nullptr;
                    FlListResult lr = ui_list(list_id, items, (u32)filtered_count, list_set,
                                              (FlListOpts) { .row_height = UI_DROPDOWN_ROW_HEIGHT,
                                                             .gap = UI_DROPDOWN_ROW_GAP,
                                                             .multi_select = false,
                                                             .reorderable = false });

                    // Searchable hand-off OUT: Nav-Up on the list's first row (nav_up_edge) returns focus to the
                    // search input. The list consumed the Nav-Up when raising the edge flag; claiming the global ring
                    // here blurs the list on the next frame.
                    if (cfg.searchable && lr.nav_up_edge) {
                        self->focused_id = search_clay_id;
                    }

                    // Commit: the mouse path picks via a per-frame selection-change (single click emits SetOne);
                    // activated_id covers double-click/Enter. Map the picked stable id back: index = id - 1.
                    u64 picked = lr.activated_id;
                    for (u32 ri = 0; picked == 0 && ri < lr.request_count; ri++) {
                        if (lr.requests[ri].kind == FlSelectionRequestKind_SetOne && lr.requests[ri].selected) {
                            picked = lr.requests[ri].id;
                        }
                    }
                    if (picked != 0) {
                        *selected_index = (i32)(picked - 1);
                        *is_open = false;
                    }
                }

                // Paired teardown of the transient highlight set.
                selection_set_destroy(highlight);

                if (cfg.show_cancel) {
                    FlString cancel_label = cfg.cancel_label.length > 0 ? cfg.cancel_label : FL_S("Cancel");
                    FlString cancel_id_str = fl_format_frame_string("%S_cancel", button_id.name);

                    FlSignal cancel_signal = ui_button_impl(cancel_label, (FlWidgetConfig) {
                                                                              .id_override = cancel_id_str,
                                                                              .focusable = FlFocusable_Yes,
                                                                              .variant = FlWidgetVariant_Ghost,
                                                                          });

                    if (cancel_signal.clicked || cancel_signal.activated) {
                        *is_open = false;
                    }
                }
            }
        }
    }
}
