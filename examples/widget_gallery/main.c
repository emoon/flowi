// A gallery of the widgets on the fl_app runner, for looking at and testing the widget set by hand. It is not a
// CI test.
//
// The renderer is chosen at runtime: GPU (the default) uses the compute backend, and --software selects the SDL
// software host.
//
// The runner's built-in default font is a 1-bit bitmap, which the GPU backend cannot rasterize because it
// computes coverage from outlines. The gallery loads a TTF through the runner's working-directory asset mount,
// so run it from the repo root.

#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include <flowi/ui/font.h>
#include <core/core.h>
#include <core/log.h>
#include <flowi/ui/feedback.h>
#include "ui/color.h"
#include "ui/layout.h"
#include "ui/ui.h"
#include "ui/ui_list.h"
#include "ui/menu/ui_menu.h"
#include "ui/ui_message_box.h"
#include "ui/ui_selection.h"
#include "ui/ui_tooltip.h"

#define GALLERY_LIST_ROWS 500
#define GALLERY_DECL_ROWS 12
typedef struct GalleryState {
    int clicks;
    bool checkbox;
    bool toggle;
    float volume;
    int count;
    bool dropdown_open;
    int dropdown_choice;
    int radio_choice;
    int tab;
    bool alert_open;
    bool confirm_open;
    FlString last_result;
    FlString last_menu_action;
    u64 list_activated;
    FlSelectionSet* list_selection;             // the List auto-applies into it
    u64 list_ids[GALLERY_LIST_ROWS];            // the app-owned ordered id slice, built once in main
    FlString list_labels[GALLERY_LIST_ROWS];    // per-row labels, handed to the List for keyboard type-ahead
    char list_label_buf[GALLERY_LIST_ROWS][16]; // backing storage the list_labels point into
    FlSelectionSet* decl_selection;
    FlListItem decl_items[GALLERY_DECL_ROWS];   // the row descriptors, built once in main
    char decl_label_buf[GALLERY_DECL_ROWS][16]; // backing storage the decl_items[].label point into
    u64 decl_activated;
    u64 decl_menu;
    bool feedback_installed; // installed on the first frame, once the UI context exists
} GalleryState;

static void section_header(FlString title) {
    ui_text_styled(title, .color = ui_color(150, 200, 255, 255));
}

static void text_from(const char* buf) {
    ui_text(fl_frame_str(buf, strlen(buf)));
}

// One framed, padded column per widget.
#define section                                                                                           \
    ui_layout_area(.direction = LayoutDirection_TopToBottom, .child_gap = 10, .padding = padding_all(16), \
                   .sizing = { .width = sizing_grow() }, .color = ui_color(32, 34, 42, 255),              \
                   .radius = corner_radius(8))

typedef struct ReorderRows {
    u64 ids[GALLERY_LIST_ROWS];
    FlString labels[GALLERY_LIST_ROWS];
    u32 count;
} ReorderRows;

static bool reorder_is_moving(const FlListResult* lr, u64 id) {
    for_count(i, lr->drag_from_count) {
        if (lr->drag_from_ids[i] == id) {
            return true;
        }
    }
    return false;
}

static void reorder_push(ReorderRows* rows, u64 id, FlString label) {
    rows->ids[rows->count] = id;
    rows->labels[rows->count] = label;
    rows->count++;
}

// Append the block when the target was itself dragged or is missing.
static u32 reorder_insert_at(const ReorderRows* kept, const FlListResult* lr) {
    for_count(i, kept->count) {
        if (kept->ids[i] == lr->drag_target_id) {
            return lr->drag_rel == FlDropRelationship_Before ? i : i + 1;
        }
    }
    return kept->count;
}

static void reorder_append(ReorderRows* out, const ReorderRows* rows, u32 first, u32 end) {
    for (u32 i = first; i < end; i++) {
        reorder_push(out, rows->ids[i], rows->labels[i]);
    }
}

// The List describes the drop and the app applies it. list_ids and the parallel list_labels move together so
// each id keeps its type-ahead label, and a multi-row selection moves as one contiguous block.
static void gallery_apply_reorder(GalleryState* s, const FlListResult* lr) {
    if (lr->drag_from_count == 0) {
        return;
    }
    static ReorderRows kept, moved, ordered;
    kept.count = 0;
    moved.count = 0;
    ordered.count = 0;
    for_count(i, GALLERY_LIST_ROWS) {
        u64 id = s->list_ids[i];
        reorder_push(reorder_is_moving(lr, id) ? &moved : &kept, id, s->list_labels[i]);
    }
    u32 insert_at = reorder_insert_at(&kept, lr);
    reorder_append(&ordered, &kept, 0, insert_at);
    reorder_append(&ordered, &moved, 0, moved.count);
    reorder_append(&ordered, &kept, insert_at, kept.count);
    memcpy(s->list_ids, ordered.ids, sizeof(s->list_ids));
    memcpy(s->list_labels, ordered.labels, sizeof(s->list_labels));
}

// Feedback sink: flowi reports what happened through the hook and the app decides the effect. This one logs
// the name of the sound an app would play.
static void gallery_feedback(const FlUiFeedbackEvent* ev, void* user_data) {
    (void)user_data;
    static const char* dir_name[] = { "none", "up", "down", "left", "right" };
    const char* dir = ev->dir < (FlUiFeedbackDir)sizeof_array(dir_name) ? dir_name[ev->dir] : "?";
    switch (ev->kind) {
        case FlUiFeedbackKind_NavMoved:
            log_info("feedback: NavMoved %s (%u -> %u) — app would play tick.wav", dir, ev->from, ev->to);
            break;
        case FlUiFeedbackKind_NavBlocked:
            log_info("feedback: NavBlocked %s (edge at %u) — app would play bump.wav", dir, ev->from);
            break;
        case FlUiFeedbackKind_Activated:
            log_info("feedback: Activated (%u) — app would play confirm.wav", ev->to);
            break;
    }
}

// The message boxes are called unconditionally before the main content. Each returns early while its open
// bool is false, and being first means the gallery behind is blocked from input on the frame a box opens.
static void message_boxes(GalleryState* s) {
    if (ui_message_box(FL_S("Heads up"), FL_S("This is a single-button alert. ESC or OK dismisses it."), &s->alert_open,
                       .confirm_label = FL_S("Got it"))
        != FlDialogResult_None) {
        s->last_result = FL_S("alert dismissed");
    }
    switch (ui_message_box(FL_S("Delete item?"), FL_S("This can't be undone."), &s->confirm_open,
                           .confirm_label = FL_S("Delete"), .cancel_label = FL_S("Keep"))) {
        case FlDialogResult_Confirmed:
            s->last_result = FL_S("confirmed (Delete)");
            break;
        case FlDialogResult_Cancelled:
            s->last_result = FL_S("cancelled (Keep / ESC)");
            break;
        case FlDialogResult_None:
            break;
    }
}

// Shortcuts are display only; the core has no accelerator dispatch.
static void menu_action(GalleryState* s, FlString label, FlString shortcut, FlString action) {
    if (ui_menu_item(label, .shortcut = shortcut).clicked) {
        s->last_menu_action = action;
    }
}

static void file_menu(GalleryState* s) {
    const FlString none = { 0 };
    menu_action(s, FL_S("New"), FL_S("Ctrl+N"), FL_S("File ▸ New"));
    menu_action(s, FL_S("Open"), FL_S("Ctrl+O"), FL_S("File ▸ Open"));
    ui_menu_separator();
    ui_menu_item(FL_S("Save"), .shortcut = FL_S("Ctrl+S"), .disabled = true); // nothing to save in the demo
    ui_submenu(FL_S("Recent")) {
        menu_action(s, FL_S("project.em"), none, FL_S("File ▸ Recent ▸ project.em"));
        ui_submenu(FL_S("Older")) {
            menu_action(s, FL_S("draft.em"), none, FL_S("File ▸ Recent ▸ Older ▸ draft.em"));
            menu_action(s, FL_S("scratch.em"), none, FL_S("File ▸ Recent ▸ Older ▸ scratch.em"));
        }
    }
}

static void edit_menu(GalleryState* s) {
    menu_action(s, FL_S("Undo"), FL_S("Ctrl+Z"), FL_S("Edit ▸ Undo"));
    menu_action(s, FL_S("Redo"), FL_S("Ctrl+Shift+Z"), FL_S("Edit ▸ Redo"));
    ui_menu_separator();
    ui_menu_header(FL_S("Clipboard"));
    menu_action(s, FL_S("Cut"), FL_S("Ctrl+X"), FL_S("Edit ▸ Cut"));
    menu_action(s, FL_S("Copy"), FL_S("Ctrl+C"), FL_S("Edit ▸ Copy"));
    menu_action(s, FL_S("Paste"), FL_S("Ctrl+V"), FL_S("Edit ▸ Paste"));
}

// The labels differ from the Checkbox and Radio cards because a layout id is derived from the label and
// identical labels would collide. The state is shared through the bound pointer.
static void view_menu(GalleryState* s) {
    ui_menu_check(FL_S("Feature enabled"), &s->checkbox);
    ui_menu_separator();
    ui_menu_header(FL_S("Quality"));
    ui_menu_radio(FL_S("Low quality"), &s->radio_choice, 0);
    ui_menu_radio(FL_S("Medium quality"), &s->radio_choice, 1);
    ui_menu_radio(FL_S("High quality"), &s->radio_choice, 2);
}

// The View menu's check and radio items are bound to the same state as the Checkbox and Radio cards, so
// changing them here changes the cards too.
static void menubar(GalleryState* s) {
    ui_menubar(ID("gallery_menubar")) {
        ui_menubar_menu(FL_S("File")) {
            file_menu(s);
        }
        ui_menubar_menu(FL_S("Edit")) {
            edit_menu(s);
        }
        ui_menubar_menu(FL_S("View")) {
            view_menu(s);
        }
    }
}

static void button_section(GalleryState* s) {
    char buf[64];
    section {
        section_header(FL_S("Button"));
        if (ui_button(FL_S("Click me")).clicked) {
            s->clicks++;
        }
        snprintf(buf, sizeof(buf), "Clicked %d time%s", s->clicks, s->clicks == 1 ? "" : "s");
        text_from(buf);
    }
}

static void toggle_sections(GalleryState* s) {
    section {
        section_header(FL_S("Checkbox"));
        ui_checkbox(FL_S("Enable feature"), &s->checkbox);
    }
    section {
        section_header(FL_S("Switch"));
        ui_switch(FL_S("Enable notifications"), &s->toggle);
    }
    section {
        section_header(FL_S("Radio group"));
        FlString options[] = { FL_S("Low"), FL_S("Medium"), FL_S("High") };
        ui_radio_group(options, (int)sizeof_array(options), &s->radio_choice);
    }
}

static void value_sections(GalleryState* s) {
    section {
        section_header(FL_S("Slider"));
        ui_slider_float(FL_S("Volume"), &s->volume, 0.0f, 1.0f);
        ui_slider_int(FL_S("Count"), &s->count, 0, 10);
    }
    section {
        section_header(FL_S("Progress bar"));
        // Driven by the Volume slider.
        ui_progress_bar(s->volume);
    }
}

static void choice_sections(GalleryState* s) {
    section {
        section_header(FL_S("Dropdown"));
        FlString options[] = { FL_S("Low"), FL_S("Medium"), FL_S("High") };
        ui_dropdown(FL_S("Quality"), &s->dropdown_open, options, (int)sizeof_array(options), &s->dropdown_choice);
    }
    section {
        section_header(FL_S("Tab bar"));
        FlString tabs[] = { FL_S("General"), FL_S("Audio"), FL_S("Video") };
        ui_tab_bar(tabs, (int)sizeof_array(tabs), &s->tab);
    }
}

static void tooltip_section(void) {
    section {
        section_header(FL_S("Tooltip"));
        // The tooltip is called right after its anchor and keyed by the anchor's layout id (the
        // button's label here).
        ui_button(FL_S("Hover for a tip"));
        ui_tooltip(ID("Hover for a tip"), FL_S("Tooltips reveal after a short hover-dwell."));
    }
}

static void message_box_section(GalleryState* s) {
    section {
        section_header(FL_S("Message box"));
        // The dialogs are emitted at the top of frame(); these buttons only set the open bools.
        if (ui_button(FL_S("Show alert")).clicked) {
            s->alert_open = true;
        }
        if (ui_button(FL_S("Show confirm")).clicked) {
            s->confirm_open = true;
        }
        if (s->last_result.length > 0) {
            ui_text(s->last_result);
        }
    }
}

static void context_menu_section(const GalleryState* s) {
    section {
        section_header(FL_S("Context menu"));
        ui_text(FL_S("Right-click inside the panel below."));
        // The region captures the right-click and opens ctx_menu at the cursor. The menu panel is
        // emitted at frame top level.
        ui_menu_region(ID("ctx_region"), ID("ctx_menu")) {
            ui_layout_area(.sizing = { .width = sizing_grow() }, .padding = padding_all(24),
                           .color = ui_color(44, 46, 56, 255), .radius = corner_radius(6),
                           .direction = LayoutDirection_TopToBottom) {
                ui_text_styled(FL_S("Right-click here"), .color = ui_color(150, 200, 255, 255));
            }
        }
        if (s->last_menu_action.length > 0) {
            ui_text(s->last_menu_action);
        }
    }
}

static void list_row(GalleryState* s, u64 id) {
    char buf[32];
    if (fl_list_item_begin(id).clicked) {
        s->list_activated = id;
    }
    ui_layout_area(.sizing = { .width = sizing_grow() }, .padding = padding_all(6), .radius = corner_radius(4)) {
        snprintf(buf, sizeof(buf), "Item %llu", (unsigned long long)id);
        text_from(buf);
    }
    fl_list_item_end();
}

static void list_apply_result(GalleryState* s, const FlListResult* lr) {
    if (lr->activated_id != 0) {
        s->list_activated = lr->activated_id;
    }
    if (lr->reorder_emitted) {
        gallery_apply_reorder(s, lr);
    }
}

// A long list in a fixed-height viewport: only the rows intersecting the viewport lay out. Each row has an
// opaque, stable u64 id, here its 1-based index. The List runs the selection gestures, applies them to the
// caller-owned set, and draws the row highlight and the marquee rectangle from set membership, so the row
// content is transparent and draws over the highlight.
static void list_section(GalleryState* s) {
    char buf[64];
    section {
        section_header(FL_S("List (virtualized, multi-select + marquee + drag-reorder)"));
        // The List forwards the full ordered id slice to the set, so a shift-range or Ctrl+A covers
        // rows that are virtualized away.
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(180) },
                       .direction = LayoutDirection_TopToBottom) {
            FlListRange r = fl_list_begin(ID("gallery_list"), s->list_ids, GALLERY_LIST_ROWS, s->list_selection,
                                          .row_height = 26.0f, .gap = 4.0f, .labels = s->list_labels);
            for (u32 i = r.start; i < r.end; i++) {
                // Reorder mutates the order, so read the stable id from the slice, not from i.
                list_row(s, s->list_ids[i]);
            }
            FlListResult lr = fl_list_end();
            list_apply_result(s, &lr);
        }
        snprintf(buf, sizeof(buf), "%llu selected (last click id %llu)",
                 (unsigned long long)selection_set_len(s->list_selection), (unsigned long long)s->list_activated);
        text_from(buf);
    }
}

static u32 decl_index_of(const GalleryState* s, u64 id) {
    for_count(i, GALLERY_DECL_ROWS) {
        if (s->decl_items[i].id == id) {
            return i;
        }
    }
    return GALLERY_DECL_ROWS;
}

static void decl_swap_items(GalleryState* s, u64 from_id, u64 to_id) {
    u32 from = decl_index_of(s, from_id);
    u32 to = decl_index_of(s, to_id);
    if (from < GALLERY_DECL_ROWS && to < GALLERY_DECL_ROWS) {
        FlListItem moved = s->decl_items[from];
        s->decl_items[from] = s->decl_items[to];
        s->decl_items[to] = moved;
    }
}

static void decl_apply_result(GalleryState* s, const FlListResult* dr) {
    if (dr->activated_id != 0) {
        s->decl_activated = dr->activated_id;
    }
    if (dr->context_menu_id != 0) {
        s->decl_menu = dr->context_menu_id; // a real app opens its own context menu at the cursor
    }
    // ui_list describes the drop and the app applies it. Here it swaps the two endpoints'
    // descriptors, which is enough to see the move.
    if (dr->reorder_emitted && dr->drag_from_count > 0) {
        decl_swap_items(s, dr->drag_from_ids[0], dr->drag_target_id);
    }
}

// One ui_list call over an array of {id, label, icon} descriptors and the caller-owned set runs the
// interaction and renders each row. The app reads the outcomes from the result.
static void decl_list_section(GalleryState* s) {
    char buf[64];
    section {
        section_header(FL_S("List (declarative ui_list — one call, label/icon rows)"));
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(150) },
                       .direction = LayoutDirection_TopToBottom) {
            FlListResult dr
                = ui_list(ID("gallery_decl_list"), s->decl_items, GALLERY_DECL_ROWS, s->decl_selection,
                          (FlListOpts) { .row_height = 26.0f, .gap = 4.0f, .multi_select = true, .reorderable = true });
            decl_apply_result(s, &dr);
        }
        snprintf(buf, sizeof(buf), "%llu selected (last open id %llu, last menu id %llu)",
                 (unsigned long long)selection_set_len(s->decl_selection), (unsigned long long)s->decl_activated,
                 (unsigned long long)s->decl_menu);
        text_from(buf);
    }
}

static void collection_sections(GalleryState* s) {
    list_section(s);
    decl_list_section(s);
}

static void widget_sections(GalleryState* s) {
    button_section(s);
    toggle_sections(s);
    value_sections(s);
    choice_sections(s);
    tooltip_section();
    message_box_section(s);
    context_menu_section(s);
    collection_sections(s);
}

// Floating context menu, emitted at frame top level so the scroll area never clips it.
static void context_menu(GalleryState* s) {
    const FlString none = { 0 };
    ui_menu(ID("ctx_menu"), .min_width = 220) {
        menu_action(s, FL_S("Cut"), FL_S("Ctrl+X"), FL_S("Context ▸ Cut"));
        menu_action(s, FL_S("Copy"), FL_S("Ctrl+C"), FL_S("Context ▸ Copy"));
        ui_menu_item(FL_S("Paste"), .shortcut = FL_S("Ctrl+V"),
                     .disabled = true); // nothing on the clipboard in the demo
        ui_menu_separator();
        if (ui_menu_item(FL_S("Keep me open"), .close_policy = FlMenuClosePolicy_KeepOpen).clicked) {
            s->last_menu_action = FL_S("Context ▸ Keep me open");
        }
        ui_submenu(FL_S("More")) {
            menu_action(s, FL_S("Duplicate"), none, FL_S("Context ▸ More ▸ Duplicate"));
            menu_action(s, FL_S("Delete"), none, FL_S("Context ▸ More ▸ Delete"));
        }
    }
}

static void frame(void* user_data) {
    GalleryState* s = (GalleryState*)user_data;

    // Installed on the first frame, once the UI context is current.
    if (!s->feedback_installed) {
        fl_ui_set_feedback_hook(gallery_feedback, s);
        s->feedback_installed = true;
    }

    message_boxes(s);

    // The outer column carries no padding so the menu bar spans the full window width, outside the scroll area.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = ui_color(20, 21, 27, 255),
                   .direction = LayoutDirection_TopToBottom, .child_gap = 0) {
        menubar(s);

        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                       .direction = LayoutDirection_TopToBottom, .child_gap = 16, .padding = padding_all(24)) {
            ui_text_styled(FL_S("flowi — Widget Gallery"), .color = ui_color(235, 235, 245, 255));

            ui_scroll_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                           .direction = LayoutDirection_TopToBottom, .child_gap = 16, .show_scrollbar = true,
                           .smooth_scroll = true) {
                widget_sections(s);
            }
        }
    }

    context_menu(s);
}

static bool parse_renderer(int argc, char** argv, FlAppRenderer* renderer) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gpu") == 0) {
            *renderer = FlAppRenderer_Gpu;
        } else if (strcmp(argv[i], "--software") == 0) {
            *renderer = FlAppRenderer_Software;
        } else {
            log_error("widget_gallery: unknown argument '%s' (use --software [default] or --gpu)", argv[i]);
            return false;
        }
    }
    return true;
}

static FlString static_label(char* buf, size_t size, const char* prefix, u32 number) {
    int n = snprintf(buf, size, "%s %u", prefix, number);
    return (FlString) { .data = buf, .length = (u64)n, .is_static = 1 };
}

static void state_init(GalleryState* state) {
    state->volume = 0.5f;
    state->count = 3;
    state->list_selection = selection_set_create(64);
    state->decl_selection = selection_set_create(16);
    // The parallel labels back keyboard type-ahead, including rows virtualized out of view.
    for_count(i, GALLERY_LIST_ROWS) {
        state->list_ids[i] = (u64)(i + 1);
        state->list_labels[i]
            = static_label(state->list_label_buf[i], sizeof(state->list_label_buf[i]), "Item", (u32)i + 1);
    }
    for_count(i, GALLERY_DECL_ROWS) {
        state->decl_items[i] = (FlListItem) {
            .id = (u64)(i + 1),
            .label = static_label(state->decl_label_buf[i], sizeof(state->decl_label_buf[i]), "Track", (u32)i + 1),
        };
    }
}

static void state_destroy(GalleryState* state) {
    selection_set_destroy(state->list_selection);
    selection_set_destroy(state->decl_selection);
}

static const char* renderer_name(FlAppRenderer renderer) {
    return renderer == FlAppRenderer_Gpu ? "GPU" : "software";
}

int main(int argc, char** argv) {
    FlAppRenderer renderer = FlAppRenderer_Gpu;
    if (!parse_renderer(argc, argv, &renderer)) {
        return 2;
    }

    FlAppSettings settings = {
        .window_title = FL_S("flowi widget gallery"),
        .width = 1280,
        .height = 720,
        .headless = false,
        .renderer = renderer,
    };

    FlApp* app = fl_app_create(&settings);
    if (!app) {
        // There is no automatic fallback: a GPU request on a build without the GPU backend fails here.
        log_error("widget_gallery: fl_app_create failed (%s renderer)", renderer_name(renderer));
        return 1;
    }

    // A failure here means the path did not resolve (run from the repo root); widgets still render.
    FlFontId font = fl_font_load(FL_S("examples/data/Roboto-Regular.ttf"), 18);
    ui_font_set(font);
    // TODO: remove this wait. Without it the first frames lay out against a font that is not ready and the
    // layout settles wrong. The API should render when the font becomes ready.
    ui_font_wait_load(font);

    static GalleryState state;
    state_init(&state);

    log_info("widget_gallery: window up at %dx%d (%s) — close the window to exit", settings.width, settings.height,
             renderer_name(renderer));
    fl_app_run(app, &state, frame);

    state_destroy(&state);
    fl_app_destroy(app);
    return 0;
}
