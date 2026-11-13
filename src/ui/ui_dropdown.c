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
#include "image/image.h"
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

typedef struct DropdownSearchKey {
    char text[256];
    u32 length;
} DropdownSearchKey;

// The search input's focus state and visible text; zeroed when the dropdown is not searchable.
typedef struct DropdownSearch {
    bool focused;
    u32 clay_id;
    FlString query;
} DropdownSearch;

// The caller's inputs for one dropdown call; the drawing steps only read it.
typedef struct Dropdown {
    FlLayoutId button_id;
    FlLayoutId list_id;
    const FlDropdownConfig* cfg;
    FlString* options;
    int option_count;
    int selected_index;
} Dropdown;

typedef struct DropdownTrigger {
    FlSignal signal;
    FlColor popup_bg;
} DropdownTrigger;

typedef struct DropdownOpenState {
    bool is_open;
    bool opened_this_frame;
} DropdownOpenState;

// What the user did inside the popup: `picked` is a stable row id (0 = none).
typedef struct DropdownOutcome {
    u64 picked;
    bool cancelled;
} DropdownOutcome;

typedef struct DropdownRows {
    FlListItem* items;
    i32 count;
} DropdownRows;

static bool dropdown_has_search(const Dropdown* d) {
    return d->cfg->searchable && d->cfg->search_state;
}

static char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

// Lowercased copy of the query, truncated to fit (null-terminated).
static DropdownSearchKey dropdown_search_key(FlString query) {
    DropdownSearchKey key = { { 0 }, 0 };
    if (query.length == 0) {
        return key;
    }
    key.length = query.length < sizeof(key.text) - 1 ? (u32)query.length : (u32)(sizeof(key.text) - 1);
    for (u32 i = 0; i < key.length; i++) {
        key.text[i] = ascii_lower(query.data[i]);
    }
    return key;
}

// Case-insensitive substring test against an already lowercased key.
static bool dropdown_option_matches(FlString option, const DropdownSearchKey* key) {
    if (option.length < key->length) {
        return false;
    }
    for (u64 start = 0; start <= option.length - key->length; start++) {
        u32 k = 0;
        while (k < key->length && ascii_lower(option.data[start + k]) == key->text[k]) {
            k++;
        }
        if (k == key->length) {
            return true;
        }
    }
    return false;
}

// Build the List's item descriptors, applying the substring filter. Each row's stable id is orig_index + 1: the
// List reserves id 0 as its "none" sentinel. Commit maps back via id - 1.
static DropdownRows dropdown_filter_items(const Dropdown* d, const DropdownSearchKey* key) {
    FlListItem* items = arena_alloc_array(g_ui_ctx->render_frame_allocator, FlListItem, d->option_count);
    FlString* options = d->options;
    i32 count = 0;
    for_count(i, d->option_count) {
        if (key->length > 0 && !dropdown_option_matches(options[i], key)) {
            continue;
        }
        items[count++] = (FlListItem) { .id = (u64)(i + 1), .label = options[i] };
    }
    return (DropdownRows) { items, count };
}

// The stable id the user picked this frame, or 0. The mouse path picks via a per-frame selection-change (a single
// click emits SetOne); activated_id covers double-click/Enter.
static u64 dropdown_picked_id(const FlListResult* lr) {
    u64 picked = lr->activated_id;
    for (u32 i = 0; picked == 0 && i < lr->request_count; i++) {
        if (lr->requests[i].kind == FlSelectionRequestKind_SetOne && lr->requests[i].selected) {
            picked = lr->requests[i].id;
        }
    }
    return picked;
}

// Draw the trigger and return its signal plus the popup panel colour from the resolved theme. The trigger is an
// ordinary single-pass variant-coloured button (widget prologue).
static DropdownTrigger dropdown_draw_trigger(FlString label, const Dropdown* d) {
    FlUiCtx* self = g_ui_ctx;
    const FlDropdownConfig* cfg = d->cfg;
    FlString current_option = d->options[d->selected_index];
    FlString button_text = label.length > 0 ? fl_format_frame_string("%S: %S", label, current_option) : current_option;

    FlWidgetVariant variant = FlWidgetVariant_Outline;
    FlWidgetConfig button_cfg = {
        .id_override = d->button_id.name,
        .theme_override = cfg->widget.theme_override,
        .color = cfg->widget.color,
        .text_color = cfg->widget.text_color,
        .focusable = cfg->widget.focusable,
        .auto_focus = cfg->widget.auto_focus,
        .tab_index = cfg->widget.tab_index,
        .variant = variant,
    };
    UiWidgetState w = ui_widget_prepare(label, button_cfg, STANDARD_WIDGET_FLAGS, variant, /*animate_text=*/false);

    // Trigger geometry; 0 in a knob means the built-in default. A non-zero padding_v switches the trigger from the
    // fixed FlWidgetHeight_MD height to padding-driven height; padding_h only insets.
    const FlLayoutPadding trigger_padding
        = fl_padding_hv(cfg->padding_h > 0.0f ? (u16)cfg->padding_h : (u16)FlSpacing_MD,
                        cfg->padding_v > 0.0f ? (u16)cfg->padding_v : (u16)FlSpacing_SM);
    const float radius = cfg->corner_radius > 0.0f ? cfg->corner_radius : (float)FlRadius_MD;
    const FlLayoutSizingAxis trigger_height = cfg->padding_v > 0.0f ? sizing_fit(0) : fl_sizing_height_md();

    ui_layout_area_id(d->button_id, .sizing = { sizing_grow(0), trigger_height },
                      .direction = LayoutDirection_LeftToRight, .padding = trigger_padding,
                      .child_alignment = ui_align(Left, Center), .child_gap = FlSpacing_SM,
                      .radius = corner_radius(radius), .color = w.colors.bg,
                      .border = fl_border(w.colors.border, FlBorderWidth_Medium)) {
        ui_text_styled_impl(button_text, (FlTextConfig) { .color = w.colors.text, .valign = FlTextVAlign_Center });

        // Spacer to push chevron to the right
        ui_layout_area(.sizing = { sizing_grow(0), sizing_fixed(1) }) {}

        ui_layout_area(.sizing = { sizing_fixed(UI_ICON_SIZE_SM), sizing_fixed(UI_ICON_SIZE_SM) },
                       .child_alignment = ui_align(Center, Center)) {
            ui_image(self->shared->icons[FlIcon_ChevronDown], .color = w.theme.text.primary);
        }
    }
    return (DropdownTrigger) { .signal = w.signal, .popup_bg = w.theme.surface.base };
}

// Searchable: the search input is the first focusable widget in the popup scope, so initial focus lands here on
// the open frame. Its visible text drives the filter; focused / clay_id feed the input<->list focus hand-off.
static DropdownSearch dropdown_draw_search(const Dropdown* d, float min_width) {
    DropdownSearch search = { 0 };
    if (!dropdown_has_search(d)) {
        return search;
    }
    FlString search_id_str = fl_format_frame_string("%S_dropdown_search", d->button_id.name);
    search.clay_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = search_id_str, .id = 0 });
    FlSignal signal = ui_text_input(search_id_str, d->cfg->search_state,
                                    (FlTextInputConfig) {
                                        .placeholder = FL_S("Search..."),
                                        .min_width = min_width,
                                        .widget = { .focusable = FlFocusable_Yes },
                                    });
    search.focused = signal.focused;
    search.query = ui_text_input_get_text(d->cfg->search_state);
    return search;
}

// On the frame a NON-searchable dropdown opens, start keyboard focus in the list, seeded on the current value, so
// arrow keys navigate and Enter/Space commit without a first click. A searchable dropdown starts focus in its
// search input instead and hands off to the list.
static void dropdown_focus_list_on_open(const Dropdown* d, bool opened_this_frame) {
    if (opened_this_frame && !d->cfg->searchable) {
        ui_list_request_focus(d->list_id, (u64)(d->selected_index + 1));
    }
}

// Searchable hand-off IN: Down while the search input holds focus moves focus into the list, seeded on the first
// filtered row. With no filtered rows Down is a no-op; either way the Nav-Down is consumed so it neither
// spatial-navigates out of the search box nor advances the list cursor.
static void dropdown_hand_off_search_to_list(const DropdownSearch* search, const DropdownRows* rows,
                                             FlLayoutId list_id) {
    if (!search->focused || !fl_get_nav_frame().nav_down) {
        return;
    }
    fl_consume_nav_down();
    if (rows->count > 0) {
        ui_list_request_focus(list_id, rows->items[0].id);
    }
}

// Height of the bounded viewport: min(count, max_visible_rows) rows; the List virtualizes/scrolls the rest.
static float dropdown_viewport_height(const FlDropdownConfig* cfg, i32 filtered_count) {
    int max_rows = cfg->max_visible_rows > 0 ? cfg->max_visible_rows : UI_DROPDOWN_DEFAULT_MAX_ROWS;
    int visible_rows = filtered_count < max_rows ? filtered_count : max_rows;
    return (float)visible_rows * (UI_DROPDOWN_ROW_HEIGHT + UI_DROPDOWN_ROW_GAP);
}

// The option list inside a bounded viewport. Returns the stable id picked this frame, or 0.
static u64 dropdown_draw_list(const Dropdown* d, const DropdownSearch* search, const DropdownRows* rows,
                              bool opened_this_frame) {
    // Transient one-element highlight set, seeded from the selected index each open frame and never read back.
    // selection_set_create allocates a heap arena: create/destroy must pair every open frame.
    FlSelectionSet* highlight = selection_set_create(1);
    selection_set_set(highlight, (u64)(d->selected_index + 1), true);

    u64 picked = 0;
    ui_layout_area(.sizing = { sizing_grow(0), sizing_fixed(dropdown_viewport_height(d->cfg, rows->count)) },
                   .direction = LayoutDirection_TopToBottom) {
        dropdown_focus_list_on_open(d, opened_this_frame);
        dropdown_hand_off_search_to_list(search, rows, d->list_id);

        // An empty (all-filtered-out) list feeds NULL ids to the core, which forbids a non-NULL set, so the set is
        // dropped on that path.
        FlSelectionSet* list_set = rows->count > 0 ? highlight : nullptr;
        FlListResult lr = ui_list(d->list_id, rows->items, (u32)rows->count, list_set,
                                  (FlListOpts) { .row_height = UI_DROPDOWN_ROW_HEIGHT,
                                                 .gap = UI_DROPDOWN_ROW_GAP,
                                                 .multi_select = false,
                                                 .reorderable = false });

        // Searchable hand-off OUT: Nav-Up on the list's first row (nav_up_edge) returns focus to the search input.
        // The list consumed the Nav-Up when raising the edge flag; claiming the global ring here blurs the list on
        // the next frame.
        if (d->cfg->searchable && lr.nav_up_edge) {
            g_ui_ctx->focused_id = search->clay_id;
        }

        picked = dropdown_picked_id(&lr);
    }

    selection_set_destroy(highlight);
    return picked;
}

// Draw the Cancel button; returns true when it was pressed.
static bool dropdown_draw_cancel(const Dropdown* d) {
    FlString cancel_label = d->cfg->cancel_label.length > 0 ? d->cfg->cancel_label : FL_S("Cancel");
    FlString cancel_id_str = fl_format_frame_string("%S_cancel", d->button_id.name);

    FlSignal cancel_signal = ui_button_impl(cancel_label, (FlWidgetConfig) {
                                                              .id_override = cancel_id_str,
                                                              .focusable = FlFocusable_Yes,
                                                              .variant = FlWidgetVariant_Ghost,
                                                          });

    return cancel_signal.clicked || cancel_signal.activated;
}

static DropdownOutcome dropdown_draw_popup_body(const Dropdown* d, FlColor popup_bg, bool opened_this_frame) {
    float min_width = d->cfg->min_width > 0.0f ? d->cfg->min_width : 200.0f;

    FlString panel_id_str = fl_format_frame_string("%S_dropdown_panel", d->button_id.name);
    FlLayoutId panel_id = (FlLayoutId) { .name = panel_id_str, .id = 0 };

    DropdownOutcome outcome = { 0 };
    ui_layout_area_id(panel_id, .sizing = { sizing_fit(min_width, 9999), sizing_fit() },
                      .direction = LayoutDirection_TopToBottom, .child_gap = 0, .padding = padding_all(FlSpacing_XS),
                      .radius = corner_radius(FlRadius_MD), .color = popup_bg) {
        DropdownSearch search = dropdown_draw_search(d, min_width);
        DropdownSearchKey key = dropdown_search_key(search.query);
        DropdownRows rows = dropdown_filter_items(d, &key);

        outcome.picked = dropdown_draw_list(d, &search, &rows, opened_this_frame);

        if (d->cfg->show_cancel) {
            outcome.cancelled = dropdown_draw_cancel(d);
        }
    }
    return outcome;
}

// Toggle on a trigger click/activation, tracking the false->true edge so a non-searchable popup requests list
// focus on exactly the frame it opens; then honor the popup's own auto-close (click-outside, ESC), which
// ui_popup_close_internal reports via popup_closed_this_frame.
static DropdownOpenState dropdown_toggle(const Dropdown* d, bool was_open, FlSignal trigger) {
    DropdownOpenState state = { .is_open = was_open, .opened_this_frame = false };
    if (trigger.clicked || trigger.activated) {
        state.is_open = !was_open;
        state.opened_this_frame = state.is_open;
        // Clear search text when opening a searchable dropdown (reset the stb cursor too, not just the length).
        if (state.is_open && dropdown_has_search(d)) {
            ui_text_input_clear(d->cfg->search_state);
        }
    }
    if (state.is_open && g_ui_ctx->popup_closed_this_frame) {
        state.is_open = false;
    }
    return state;
}

static DropdownOutcome dropdown_show_popup(const Dropdown* d, DropdownTrigger trigger, bool opened_this_frame) {
    DropdownOutcome outcome = { 0 };
    ui_popup(d->button_id, .offset = { 0, 5 }, .z_index = UI_Z_OVERLAY, .auto_close = true, .capture_input = true) {
        outcome = dropdown_draw_popup_body(d, trigger.popup_bg, opened_this_frame);
    }
    return outcome;
}

// Write a pick back (index = id - 1). Returns true when the popup should close.
static bool dropdown_commit(DropdownOutcome outcome, int* selected_index) {
    if (outcome.picked != 0) {
        *selected_index = (i32)(outcome.picked - 1);
    }
    return outcome.picked != 0 || outcome.cancelled;
}

void ui_dropdown_impl(FlString label, bool* is_open, FlString* options, int option_count, int* selected_index,
                      FlDropdownConfig cfg) {
    if (!is_open || !options || option_count <= 0 || !selected_index) {
        log_error("ui_dropdown_impl: invalid parameters");
        return;
    }

    if (*selected_index < 0 || *selected_index >= option_count) {
        *selected_index = 0;
    }

    // The button id must match the popup anchor.
    FlLayoutId button_id = resolve_widget_id(label, cfg.widget.id_override);
    FlString list_id_str = fl_format_frame_string("%S_dropdown_list", button_id.name);
    const Dropdown d = {
        .button_id = button_id,
        .list_id = (FlLayoutId) { .name = list_id_str, .id = 0 },
        .cfg = &cfg,
        .options = options,
        .option_count = option_count,
        .selected_index = *selected_index,
    };
    DropdownTrigger trigger = dropdown_draw_trigger(label, &d);
    DropdownOpenState open = dropdown_toggle(&d, *is_open, trigger.signal);

    DropdownOutcome outcome
        = open.is_open ? dropdown_show_popup(&d, trigger, open.opened_this_frame) : (DropdownOutcome) { 0 };
    *is_open = open.is_open && !dropdown_commit(outcome, selected_index);
}
