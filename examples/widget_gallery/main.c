// examples/widget_gallery - a runnable gallery of the Batch-1 widgets on the fl_app runner.
//
// A standalone window driven entirely through the fl_app C ABI, used to eyeball and manually test the widget
// set - button, checkbox, switch, slider, dropdown, tab bar. Manual demo only: the monorepo never compiles it
// and it is not a CI test.
//
// The renderer is chosen at runtime: GPU (the default) drives the all-compute backend, --software selects the
// always-available SDL software host.
//
// Text needs an outline font: the runner's built-in default is a 1-bit bitmap the GPU backend cannot
// rasterize (it computes coverage from outlines), so the gallery loads a real TTF through the
// runner's working-directory asset mount - run it from the repo root.

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
#include "ui/ui_menu.h"
#include "ui/ui_message_box.h"
#include "ui/ui_scroll.h"
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
    FlSelectionSet* list_selection;             // lazy-created; the List auto-applies into it
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

// One framed, padded column per widget.
#define section                                                                                           \
    ui_layout_area(.direction = LayoutDirection_TopToBottom, .child_gap = 10, .padding = padding_all(16), \
                   .sizing = { .width = sizing_grow() }, .color = ui_color(32, 34, 42, 255),              \
                   .radius = corner_radius(8))

// The List describes the drop; the app owns the mutation. list_ids and the parallel list_labels move in
// lockstep so the (id, type-ahead label) pairing survives, and a multi-row selection moves as one contiguous block.
static void gallery_apply_reorder(GalleryState* s, const FlListResult* lr) {
    if (lr->drag_from_count == 0) {
        return;
    }
    static u64 kept_ids[GALLERY_LIST_ROWS], mov_ids[GALLERY_LIST_ROWS];
    static FlString kept_labels[GALLERY_LIST_ROWS], mov_labels[GALLERY_LIST_ROWS];
    u32 kc = 0, mc = 0;
    for (u32 i = 0; i < GALLERY_LIST_ROWS; i++) {
        u64 id = s->list_ids[i];
        bool moving = false;
        for (u32 j = 0; j < lr->drag_from_count; j++) {
            if (lr->drag_from_ids[j] == id) {
                moving = true;
                break;
            }
        }
        if (moving) {
            mov_ids[mc] = id;
            mov_labels[mc] = s->list_labels[i];
            mc++;
        } else {
            kept_ids[kc] = id;
            kept_labels[kc] = s->list_labels[i];
            kc++;
        }
    }
    u32 insert_at = kc; // default: target was itself dragged or missing -> append the block
    for (u32 i = 0; i < kc; i++) {
        if (kept_ids[i] == lr->drag_target_id) {
            insert_at = (lr->drag_rel == FlDropRelationship_Before) ? i : i + 1;
            break;
        }
    }
    u32 w = 0;
    for (u32 i = 0; i < insert_at; i++, w++) {
        s->list_ids[w] = kept_ids[i];
        s->list_labels[w] = kept_labels[i];
    }
    for (u32 i = 0; i < mc; i++, w++) {
        s->list_ids[w] = mov_ids[i];
        s->list_labels[w] = mov_labels[i];
    }
    for (u32 i = insert_at; i < kc; i++, w++) {
        s->list_ids[w] = kept_ids[i];
        s->list_labels[w] = kept_labels[i];
    }
}

// Proof-of-concept feedback sink: flowi reports what happened through the hook, the app owns the
// effect. With no audio subsystem in the monorepo, the "audio service" is stubbed to a log line naming the sound
// the app would play.
static void gallery_feedback(const FlUiFeedbackEvent* ev, void* user_data) {
    (void)user_data;
    static const char* dir_name[] = { "none", "up", "down", "left", "right" };
    const char* dir = ev->dir < (FlUiFeedbackDir)(sizeof(dir_name) / sizeof(dir_name[0])) ? dir_name[ev->dir] : "?";
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

static void frame(void* user_data) {
    GalleryState* s = (GalleryState*)user_data;

    // Installed on the first frame, once the UI context is current.
    if (!s->feedback_installed) {
        fl_ui_set_feedback_hook(gallery_feedback, s);
        s->feedback_installed = true;
    }

    char buf[64];

    // Message boxes are called UNCONDITIONALLY before the main content: each early-returns while its open bool is
    // false, and placing them first means the opening frame already blocks input to the gallery behind them.
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

    // The outer column carries no padding so the menu bar spans the full window width, outside the scroll area.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = ui_color(20, 21, 27, 255),
                   .direction = LayoutDirection_TopToBottom, .child_gap = 0) {
        // View hosts the stateful kinds bound to the SAME state the Checkbox/Radio cards use, so flipping them here
        // moves those cards too. Shortcuts are display-only (the core has no accelerator dispatch).
        ui_menubar(ID("gallery_menubar")) {
            ui_menubar_menu(FL_S("File")) {
                if (ui_menu_item(FL_S("New"), .shortcut = FL_S("Ctrl+N")).clicked) {
                    s->last_menu_action = FL_S("File ▸ New");
                }
                if (ui_menu_item(FL_S("Open"), .shortcut = FL_S("Ctrl+O")).clicked) {
                    s->last_menu_action = FL_S("File ▸ Open");
                }
                ui_menu_separator();
                ui_menu_item(FL_S("Save"), .shortcut = FL_S("Ctrl+S"), .disabled = true); // nothing to save in the demo
                ui_submenu(FL_S("Recent")) {
                    if (ui_menu_item(FL_S("project.em")).clicked) {
                        s->last_menu_action = FL_S("File ▸ Recent ▸ project.em");
                    }
                    ui_submenu(FL_S("Older")) {
                        if (ui_menu_item(FL_S("draft.em")).clicked) {
                            s->last_menu_action = FL_S("File ▸ Recent ▸ Older ▸ draft.em");
                        }
                        if (ui_menu_item(FL_S("scratch.em")).clicked) {
                            s->last_menu_action = FL_S("File ▸ Recent ▸ Older ▸ scratch.em");
                        }
                    }
                }
            }
            ui_menubar_menu(FL_S("Edit")) {
                if (ui_menu_item(FL_S("Undo"), .shortcut = FL_S("Ctrl+Z")).clicked) {
                    s->last_menu_action = FL_S("Edit ▸ Undo");
                }
                if (ui_menu_item(FL_S("Redo"), .shortcut = FL_S("Ctrl+Shift+Z")).clicked) {
                    s->last_menu_action = FL_S("Edit ▸ Redo");
                }
                ui_menu_separator();
                ui_menu_header(FL_S("Clipboard"));
                if (ui_menu_item(FL_S("Cut"), .shortcut = FL_S("Ctrl+X")).clicked) {
                    s->last_menu_action = FL_S("Edit ▸ Cut");
                }
                if (ui_menu_item(FL_S("Copy"), .shortcut = FL_S("Ctrl+C")).clicked) {
                    s->last_menu_action = FL_S("Edit ▸ Copy");
                }
                if (ui_menu_item(FL_S("Paste"), .shortcut = FL_S("Ctrl+V")).clicked) {
                    s->last_menu_action = FL_S("Edit ▸ Paste");
                }
            }
            ui_menubar_menu(FL_S("View")) {
                // Labels differ from the Checkbox/Radio cards on purpose: a layout id is derived from the label, so
                // reusing a card's exact label would collide. State is shared through the bound pointer, not the
                // label, so flipping these still moves the cards below.
                ui_menu_check(FL_S("Feature enabled"), &s->checkbox);
                ui_menu_separator();
                ui_menu_header(FL_S("Quality"));
                ui_menu_radio(FL_S("Low quality"), &s->radio_choice, 0);
                ui_menu_radio(FL_S("Medium quality"), &s->radio_choice, 1);
                ui_menu_radio(FL_S("High quality"), &s->radio_choice, 2);
            }
        }

        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                       .direction = LayoutDirection_TopToBottom, .child_gap = 16, .padding = padding_all(24)) {
            ui_text_styled(FL_S("flowi — Widget Gallery"), .color = ui_color(235, 235, 245, 255));

            ui_scroll_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                           .direction = LayoutDirection_TopToBottom, .child_gap = 16, .show_scrollbar = true,
                           .smooth_scroll = true) {
                section {
                    section_header(FL_S("Button"));
                    if (ui_button(FL_S("Click me")).clicked) {
                        s->clicks++;
                    }
                    snprintf(buf, sizeof(buf), "Clicked %d time%s", s->clicks, s->clicks == 1 ? "" : "s");
                    ui_text(fl_frame_str(buf, strlen(buf)));
                }

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
                    ui_radio_group(options, (int)(sizeof(options) / sizeof(options[0])), &s->radio_choice);
                }

                section {
                    section_header(FL_S("Slider"));
                    ui_slider_float(FL_S("Volume"), &s->volume, 0.0f, 1.0f);
                    ui_slider_int(FL_S("Count"), &s->count, 0, 10);
                }

                section {
                    section_header(FL_S("Progress bar"));
                    // Driven off the Volume slider so the bar visibly tracks it.
                    ui_progress_bar(s->volume);
                }

                section {
                    section_header(FL_S("Dropdown"));
                    FlString options[] = { FL_S("Low"), FL_S("Medium"), FL_S("High") };
                    ui_dropdown(FL_S("Quality"), &s->dropdown_open, options,
                                (int)(sizeof(options) / sizeof(options[0])), &s->dropdown_choice);
                }

                section {
                    section_header(FL_S("Tab bar"));
                    FlString tabs[] = { FL_S("General"), FL_S("Audio"), FL_S("Video") };
                    ui_tab_bar(tabs, (int)(sizeof(tabs) / sizeof(tabs[0])), &s->tab);
                }

                section {
                    section_header(FL_S("Tooltip"));
                    // The tooltip is called right after its anchor and keyed by the anchor's layout id (the
                    // button's label here).
                    ui_button(FL_S("Hover for a tip"));
                    ui_tooltip(ID("Hover for a tip"), FL_S("Tooltips reveal after a short hover-dwell."));
                }

                section {
                    section_header(FL_S("Message box"));
                    // The dialogs themselves are emitted at the top of frame(); these buttons just flip the
                    // caller-owned open bools.
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

                section {
                    section_header(FL_S("Context menu"));
                    ui_text(FL_S("Right-click inside the panel below."));
                    // The region captures the right-click and opens ctx_menu at the cursor; the menu panel itself is
                    // emitted (and floats) at frame top level, below.
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

                section {
                    section_header(FL_S("List (virtualized, multi-select + marquee + drag-reorder)"));
                    // A long list in a fixed-height viewport: only the rows intersecting the viewport lay out. Each
                    // row carries an opaque u64 stable id - here its 1-based index. The List runs the selection
                    // gestures internally, auto-applies them into the caller-owned set, and draws the row highlight
                    // and the marquee rectangle from set membership - so the row content is transparent and just
                    // composes over the highlight.
                    if (s->list_selection == nullptr) {
                        s->list_selection = selection_set_create(64);
                    }
                    // The List forwards the full ordered id slice to the set, so a shift-range or Ctrl+A resolves
                    // across rows virtualized away - not just the visible window.
                    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(180) },
                                   .direction = LayoutDirection_TopToBottom) {
                        FlListRange r = ui_list_begin(
                            ID("gallery_list"), s->list_ids, GALLERY_LIST_ROWS, s->list_selection,
                            (FlListConfig) { .row_height = 26.0f, .gap = 4.0f, .labels = s->list_labels });
                        for (u32 i = r.start; i < r.end; i++) {
                            // Reorder mutates the order, so read the stable id from the slice, not from i.
                            const u64 id = s->list_ids[i];
                            if (ui_list_item_begin(id).clicked) {
                                s->list_activated = id;
                            }
                            ui_layout_area(.sizing = { .width = sizing_grow() }, .padding = padding_all(6),
                                           .radius = corner_radius(4)) {
                                snprintf(buf, sizeof(buf), "Item %llu", (unsigned long long)id);
                                ui_text(fl_frame_str(buf, strlen(buf)));
                            }
                            ui_list_item_end();
                        }
                        FlListResult lr = ui_list_end();
                        if (lr.activated_id != 0) {
                            s->list_activated = lr.activated_id;
                        }
                        if (lr.reorder_emitted) {
                            gallery_apply_reorder(s, &lr);
                        }
                    }
                    snprintf(buf, sizeof(buf), "%llu selected (last click id %llu)",
                             (unsigned long long)selection_set_len(s->list_selection),
                             (unsigned long long)s->list_activated);
                    ui_text(fl_frame_str(buf, strlen(buf)));
                }

                section {
                    section_header(FL_S("List (declarative ui_list — one call, label/icon rows)"));
                    // The declarative face: one ui_list call over an array of {id, label, icon} descriptors plus the
                    // caller-owned set runs the WHOLE interaction internally and renders each row itself - no
                    // per-item loop. The app reads only the policy outcomes off the result.
                    if (s->decl_selection == nullptr) {
                        s->decl_selection = selection_set_create(16);
                    }
                    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(150) },
                                   .direction = LayoutDirection_TopToBottom) {
                        FlListResult dr = ui_list(
                            ID("gallery_decl_list"), s->decl_items, GALLERY_DECL_ROWS, s->decl_selection,
                            (FlListOpts) {
                                .row_height = 26.0f, .gap = 4.0f, .multi_select = true, .reorderable = true });
                        if (dr.activated_id != 0) {
                            s->decl_activated = dr.activated_id;
                        }
                        if (dr.context_menu_id != 0) {
                            s->decl_menu = dr.context_menu_id; // a real app opens its own context menu at the cursor
                        }
                        // The face describes the drop; the app owns the mutation - here just a swap of the two
                        // endpoints' descriptors, enough to see the move across frames.
                        if (dr.reorder_emitted && dr.drag_from_count > 0) {
                            u32 from = GALLERY_DECL_ROWS, to = GALLERY_DECL_ROWS;
                            for (u32 i = 0; i < GALLERY_DECL_ROWS; i++) {
                                if (s->decl_items[i].id == dr.drag_from_ids[0]) {
                                    from = i;
                                }
                                if (s->decl_items[i].id == dr.drag_target_id) {
                                    to = i;
                                }
                            }
                            if (from < GALLERY_DECL_ROWS && to < GALLERY_DECL_ROWS) {
                                FlListItem moved = s->decl_items[from];
                                s->decl_items[from] = s->decl_items[to];
                                s->decl_items[to] = moved;
                            }
                        }
                    }
                    snprintf(buf, sizeof(buf), "%llu selected (last open id %llu, last menu id %llu)",
                             (unsigned long long)selection_set_len(s->decl_selection),
                             (unsigned long long)s->decl_activated, (unsigned long long)s->decl_menu);
                    ui_text(fl_frame_str(buf, strlen(buf)));
                }
            }
        }
    }

    // Floating context menu, emitted at frame top level so the scroll area never clips it.
    ui_menu(ID("ctx_menu"), .min_width = 220) {
        if (ui_menu_item(FL_S("Cut"), .shortcut = FL_S("Ctrl+X")).clicked) {
            s->last_menu_action = FL_S("Context ▸ Cut");
        }
        if (ui_menu_item(FL_S("Copy"), .shortcut = FL_S("Ctrl+C")).clicked) {
            s->last_menu_action = FL_S("Context ▸ Copy");
        }
        ui_menu_item(FL_S("Paste"), .shortcut = FL_S("Ctrl+V"),
                     .disabled = true); // nothing on the clipboard in the demo
        ui_menu_separator();
        if (ui_menu_item(FL_S("Keep me open"), .close_policy = FlMenuClosePolicy_KeepOpen).clicked) {
            s->last_menu_action = FL_S("Context ▸ Keep me open");
        }
        ui_submenu(FL_S("More")) {
            if (ui_menu_item(FL_S("Duplicate")).clicked) {
                s->last_menu_action = FL_S("Context ▸ More ▸ Duplicate");
            }
            if (ui_menu_item(FL_S("Delete")).clicked) {
                s->last_menu_action = FL_S("Context ▸ More ▸ Delete");
            }
        }
    }
}

int main(int argc, char** argv) {
    FlAppRenderer renderer = FlAppRenderer_Gpu;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gpu") == 0) {
            renderer = FlAppRenderer_Gpu;
        } else if (strcmp(argv[i], "--software") == 0) {
            renderer = FlAppRenderer_Software;
        } else {
            log_error("widget_gallery: unknown argument '%s' (use --software [default] or --gpu)", argv[i]);
            return 2;
        }
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
        // No automatic fallback: a GPU request on a build without the GPU backend lands here.
        log_error("widget_gallery: fl_app_create failed (%s renderer)",
                  renderer == FlAppRenderer_Gpu ? "GPU" : "software");
        return 1;
    }

    // A failure here only means the path didn't resolve (run from the repo root); widgets still render.
    FlFontId font = fl_font_load(FL_S("examples/data/Roboto-Regular.ttf"), 18);
    ui_font_set(font);
    // TEMPORARY: block until the font is ready. Without it the first frames lay out against a not-yet-ready
    // font and the layout settles wrong. Render-when-ready is the goal (the API should not need this).
    ui_font_wait_load(font);

    GalleryState state = {
        .volume = 0.5f,
        .count = 3,
    };
    // The parallel labels back keyboard type-ahead, including rows virtualized out of view.
    for (u32 i = 0; i < GALLERY_LIST_ROWS; i++) {
        state.list_ids[i] = (u64)(i + 1);
        int n = snprintf(state.list_label_buf[i], sizeof(state.list_label_buf[i]), "Item %u", i + 1);
        state.list_labels[i] = (FlString) { .data = state.list_label_buf[i], .length = (u64)n, .is_static = 1 };
    }
    for (u32 i = 0; i < GALLERY_DECL_ROWS; i++) {
        int n = snprintf(state.decl_label_buf[i], sizeof(state.decl_label_buf[i]), "Track %u", i + 1);
        state.decl_items[i] = (FlListItem) {
            .id = (u64)(i + 1),
            .label = (FlString) { .data = state.decl_label_buf[i], .length = (u64)n, .is_static = 1 },
        };
    }

    log_info("widget_gallery: window up at %dx%d (%s) — close the window to exit", settings.width, settings.height,
             renderer == FlAppRenderer_Gpu ? "GPU" : "software");
    fl_app_run(app, &state, frame);

    if (state.list_selection != nullptr) {
        selection_set_destroy(state.list_selection);
    }
    if (state.decl_selection != nullptr) {
        selection_set_destroy(state.decl_selection);
    }
    fl_app_destroy(app);
    return 0;
}
