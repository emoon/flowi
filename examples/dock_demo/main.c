// Builds a first-run dock layout in code, then drives the panel-author API (titles, hide/show/close, the active
// panel, drag-to-dock) from one window.
//
// With no saved layout, an app declares its initial dock tree as a nested dock_split / dock_leaf literal
// (ui/dock.h) and lowers it with dock_tree_build, which runs dock_tree_check_invariants and returns nullptr for
// a malformed tree. dock_workspace_set_tree installs the tree into the shared workspace.
//
// Each frame the app declares every Detachable Panel with dock_panel(id, title){...}, hidden ones included. The
// workspace binds each id to the leaf that holds it and runs the body only for a leaf's selected tab.
// dock_workspace_emit tiles the tree (Clay computes every rect) and runs the drag gesture.
//
// Controls: 1-7 hide/show a panel, Shift+1-7 close it (a later 1-7 re-seats it on the floor), and each panel
// binds one letter that fires only while that panel is the active one.
//
// The default CMake build compiles it but CI never runs it: it is GPU only and has no goldens. README.md has the
// manual checklist.

#include <stdio.h>

#include "app/app.h"
#include <flowi/ui/font.h>
#include <core/core.h>
#include <core/log.h>
#include "ui/color.h"
#include "ui/dock.h"
#include "ui/dock/dock_workspace.h"
#include "ui/layout.h"
#include "ui/ui.h"

// Detachable Panel ids. An id names the same panel wherever it is docked.
enum {
    PANEL_NAVIGATOR = 1,
    PANEL_VIEWPORT,
    PANEL_INSPECTOR,
    PANEL_ASSETS,
    PANEL_CONSOLE,
    PANEL_LOG,
    PANEL_TIMELINE,
};

// Dock families. The workspace's filter is 0 (accept any), so both dock here; a second workspace could filter
// on the tags.
enum {
    FAMILY_EDIT = 1,
    FAMILY_TOOL = 2,
};

#define DEMO_PANEL_COUNT 7

typedef struct DemoPanel {
    FlDockPanelId id;
    FlString title;
    FlColor tint;
    FlKey shortcut; // a letter key, fired only while this panel is the active one
    u32 family;
} DemoPanel;

static const DemoPanel PANELS[DEMO_PANEL_COUNT] = {
    { PANEL_NAVIGATOR, FL_S_("Navigator"), { 70, 110, 160, 255 }, FlKey_N, FAMILY_EDIT },
    { PANEL_VIEWPORT, FL_S_("Viewport"), { 80, 140, 90, 255 }, FlKey_V, FAMILY_EDIT },
    { PANEL_INSPECTOR, FL_S_("Inspector"), { 150, 120, 60, 255 }, FlKey_I, FAMILY_EDIT },
    { PANEL_ASSETS, FL_S_("Assets"), { 120, 80, 130, 255 }, FlKey_A, FAMILY_EDIT },
    { PANEL_CONSOLE, FL_S_("Console"), { 160, 110, 70, 255 }, FlKey_C, FAMILY_TOOL },
    { PANEL_LOG, FL_S_("Log"), { 90, 100, 130, 255 }, FlKey_G, FAMILY_TOOL },
    { PANEL_TIMELINE, FL_S_("Timeline"), { 60, 130, 140, 255 }, FlKey_T, FAMILY_TOOL },
};

// The first-run layout: a navigator on the left, and on the right a viewport beside an inspector/assets tab
// group, above a console/log/timeline tab group. Three split levels, so the deep side reaches the tile floor.
// The compound literals are at file scope and have static storage duration, so dock_tree_build can lower the
// tree at startup.
static const FlDockSpec* DOCK_DEMO_DEFAULT_LAYOUT = dock_split(
    FlDockAxis_X, 0.20f, dock_leaf(PANEL_NAVIGATOR),
    dock_split(FlDockAxis_Y, 0.72f,
               dock_split(FlDockAxis_X, 0.70f, dock_leaf(PANEL_VIEWPORT), dock_leaf(PANEL_INSPECTOR, PANEL_ASSETS)),
               dock_leaf(PANEL_CONSOLE, PANEL_LOG, PANEL_TIMELINE)));

typedef struct DockDemoState {
    FlFontId font;
    u32 fires[DEMO_PANEL_COUNT]; // per-panel shortcut hit counts, printed in the panel body
} DockDemoState;

// Declares one panel: a tinted card showing the panel's name, whether it is the active one, its family tag and
// how often its shortcut has fired.
static void declare_panel(const FlDockWorkspace* ws, const DemoPanel* p, u32* fires) {
    dock_panel(p->id, p->title) {
        const bool active = ws->active_panel == p->id;
        if (active && ui_key_pressed(p->shortcut)) {
            (*fires)++;
        }

        // The dock_panel scope opens inside the leaf's content area, so this card grows to fill the tile.
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = p->tint,
                       .padding = padding_all(12), .direction = LayoutDirection_TopToBottom, .child_gap = 6,
                       .child_alignment = ui_align(Left, Top)) {
            ui_text(p->title);

            char buf[96];
            int n = snprintf(buf, sizeof(buf), "%s | family %u | [%c] fired %u", active ? "ACTIVE" : "inactive",
                             p->family, (char)('A' + p->shortcut - FlKey_A), *fires);
            ui_text(fl_frame_str(buf, (size_t)n));
        }
    }
}

// 1-7 hide or show a panel and Shift+1-7 close it. A hidden panel stays declared and keeps its slot; a closed
// one leaves the tree.
static void apply_visibility_keys(FlDockWorkspace* ws) {
    const bool shift = (ui_keyboard_modifiers() & FlKeyModifier_Shift) != 0;

    for (u32 i = 0; i < DEMO_PANEL_COUNT; i++) {
        if (!ui_key_pressed((FlKey)(FlKey_Num1 + i))) {
            continue;
        }

        const FlDockPanelId id = PANELS[i].id;
        if (shift) {
            dock_tree_apply(ws->tree, &(FlDockRequest) { .kind = FlDockRequest_Close, .panel = id });
        } else if (dock_workspace_panel_leaf(ws, id) == 0) {
            // A closed panel has no leaf. Showing it re-seats it on the floor, so the key that shows a hidden
            // panel also brings back a closed one.
            dock_workspace_set_panel_visible(ws, id, true);
        } else {
            dock_workspace_set_panel_visible(ws, id, !dock_workspace_panel_visible(ws, id));
        }
    }
}

static void dock_demo_frame(void* user_data) {
    DockDemoState* st = (DockDemoState*)user_data;
    ui_font_set(st->font);

    FlDockWorkspace* ws = dock_workspace_current();
    apply_visibility_keys(ws);

    // Panels not declared between here and dock_workspace_end fall back to reserved.
    dock_workspace_begin(ws);

    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = ui_color(22, 24, 30, 255),
                   .direction = LayoutDirection_TopToBottom, .child_gap = 12, .padding = padding_all(16)) {
        ui_text_styled(FL_S("dock_demo — 1-7 hide/show, Shift+1-7 close, click a panel then press its bracketed "
                            "letter, drag a tab to re-dock"),
                       .color = ui_color(235, 235, 245, 255));

        // While a tab is being dragged, dock_workspace_emit tiles a provisional tree that makes space where the
        // panel would land and fills that gap with an overlay carrying the panel's title.
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                       .color = ui_color(40, 44, 54, 255)) {
            dock_workspace_emit(ws);
        }

        // Each card attaches by id to the tile its panel is docked in, so the order here has no effect on screen
        // position. Hidden panels and background tabs are declared too; their bodies do not run.
        for (u32 i = 0; i < DEMO_PANEL_COUNT; i++) {
            declare_panel(ws, &PANELS[i], &st->fires[i]);
        }
    }

    dock_workspace_end(ws);
}

int main(void) {
    // The docking demo needs the GPU renderer; there is no software path.
    FlAppSettings settings = {
        .window_title = FL_S("flowi — dock_demo"),
        .width = 900,
        .height = 640,
        .headless = false,
        .renderer = FlAppRenderer_Gpu,
    };
    FlApp* app = fl_app_create(&settings);
    if (!app) {
        log_error("dock_demo: fl_app_create failed (GPU renderer required; build with the GPU backend)");
        return 1;
    }

    // The GPU backend rasterizes outlines, so load a TTF through the runner's working-directory asset mount.
    FlFontId font = fl_font_load(FL_S("examples/data/Roboto-Regular.ttf"), 18);
    ui_font_set(font);
    ui_font_wait_load(font);

    // dock_tree_build returns nullptr when the literal fails the invariant check.
    FlDockTree* tree = dock_tree_build(DOCK_DEMO_DEFAULT_LAYOUT);
    if (!tree) {
        log_error("dock_demo: default layout literal is invalid (failed dock_tree_check_invariants)");
        fl_app_destroy(app);
        return 1;
    }

    FlDockWorkspace* ws = dock_workspace_current();
    dock_workspace_set_tree(ws, tree);

    // Families are not persisted, so the app sets them every session.
    for (u32 i = 0; i < DEMO_PANEL_COUNT; i++) {
        dock_workspace_set_panel_family(ws, PANELS[i].id, PANELS[i].family);
    }

    log_info("dock_demo: authored default layout up — navigator | ((viewport | inspector+assets) / "
             "console+log+timeline)");
    DockDemoState state = { .font = font };
    fl_app_run(app, &state, dock_demo_frame);

    fl_app_destroy(app);
    return 0;
}
