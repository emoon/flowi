// Dock promote and demote through a stub host and the headless fl_app.

#include "dock/common.h"

// The promote and demote lifecycle through FlDockHost with a stub host and no OS window. dock_promote_panel
// enqueues. dock_workspace_begin drains the queue: it creates a second workspace, moves the subtree in and calls
// the host to open a window. fl_dock_notify_window_closed runs the demote and removes the second workspace.
typedef struct StubDockHost {
    int open_calls;
    int close_calls;
    FlDockWorkspace* last_ws;
    void* handle;
    bool return_null;     // when set, open_promoted_window fails and returns nullptr
    void* closes_on_open; // a window the host reports closed from inside open_promoted_window, or nullptr
} StubDockHost;

static void* stub_dock_open(void* ud, FlDockWorkspace* ws, f32 w, f32 h) {
    StubDockHost* s = (StubDockHost*)ud;
    s->open_calls++;
    s->last_ws = ws;
    if (s->closes_on_open != nullptr) {
        fl_dock_notify_window_closed(s->closes_on_open);
    }
    (void)w;
    (void)h;
    return s->return_null ? nullptr : s->handle;
}

static void stub_dock_close(void* ud, void* host_window) {
    StubDockHost* s = (StubDockHost*)ud;
    s->close_calls++;
    (void)host_window;
}

// The stub host registered with the shared tier, plus the default workspace's tree swapped for spec. The host and
// its window handle are file-static, so the shared tier never holds a stack address. The scope clears the
// registration and restores the tree when the case returns.
static StubDockHost s_stub_host;
static int s_stub_host_handle;

typedef struct DockStubHostScope {
    DockDefaultTree tree;
} DockStubHostScope;

static DockStubHostScope dock_stub_host_begin(FlUiCtx* fixture, const FlDockSpec* spec) {
    s_stub_host = (StubDockHost) { .handle = &s_stub_host_handle };
    DockStubHostScope scope = { .tree = dock_default_tree_swap(fixture, spec) };
    fl_dock_register_host((FlDockHost) {
        .user_data = &s_stub_host, .open_promoted_window = stub_dock_open, .close_promoted_window = stub_dock_close });
    return scope;
}

static void dock_stub_host_end(DockStubHostScope* scope) {
    fl_dock_register_host((FlDockHost) { 0 });
    dock_default_tree_restore(&scope->tree);
}

#define DOCK_STUB_HOST(var, fixture, spec) \
    __attribute__((cleanup(dock_stub_host_end))) DockStubHostScope var = dock_stub_host_begin((fixture), (spec))

// A case with the stub host registered and the default workspace holding panels 101 and 102 side by side.
#define DOCK_PROMOTE_CASE(fixture, scope, src)                                                      \
    FlUiCtx* fixture = render_fixture_get();                                                        \
    ASSERT_TRUE_MSG(fixture != nullptr, "render fixture creation failed");                          \
    DOCK_STUB_HOST(scope, fixture, dock_split(FlDockAxis_X, 0.5f, dock_leaf(101), dock_leaf(102))); \
    FlDockWorkspace* src = (scope).tree.ws

// One frame of ws: begin drains the promote queue and end closes the workspace.
static void dock_run_frame(FlDockWorkspace* ws) {
    dock_workspace_begin(ws);
    dock_workspace_end(ws);
}

// Asks for panel to be promoted and runs one frame of ws.
static void dock_promote_and_run(FlDockWorkspace* ws, FlDockPanelId panel) {
    dock_promote_panel(panel);
    dock_run_frame(ws);
}

// Promotes panel while the active context points at from, then drains the queue in src's frame.
static void dock_promote_from(FlUiCtx* fixture, FlDockWorkspace* from, FlDockWorkspace* src, FlDockPanelId panel) {
    ui_ctx_set_dock_workspace(fixture, from);
    dock_promote_panel(panel);
    ui_ctx_set_dock_workspace(fixture, src);
    dock_run_frame(src);
}

// Promotes panel 101 into a first window that then also holds panel 103. Null when the host did not open exactly
// one window or the panel did not dock.
static FlDockWorkspace* dock_open_first_window(FlDockWorkspace* src) {
    dock_promote_and_run(src, 101);
    FlDockWorkspace* first = s_stub_host.last_ws;
    if (s_stub_host.open_calls != 1 || first == nullptr) {
        return nullptr;
    }
    return dock_tab_into(first->tree, first->tree->root->id, 103) ? first : nullptr;
}

UTEST(dock_workspace, promote_demote_lifecycle_via_host) {
    DOCK_PROMOTE_CASE(fixture, scope, src);
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 2);

    // The promote is deferred, so nothing happens until dock_workspace_begin.
    dock_promote_panel(101);
    ASSERT_EQ(s_stub_host.open_calls, 0);

    dock_run_frame(src);
    ASSERT_EQ(s_stub_host.open_calls, 1);
    ASSERT_TRUE_MSG(s_stub_host.last_ws != nullptr, "host received the new (second) workspace");
    FlDockWorkspace* dst = s_stub_host.last_ws;

    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src->tree, 101) == nullptr);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(dst->tree, 101) != nullptr);
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 1);
    ASSERT_EQ((int)dock_tree_collect_panels(dst->tree, nullptr, 0), 1);

    fl_dock_notify_window_closed(&s_stub_host_handle);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src->tree, 101) != nullptr);
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 2);
    ASSERT_EQ_MSG(s_stub_host.close_calls, 0,
                  "the window is already gone on this path, so flowi must not ask the host to close it again");

    // An unknown handle does nothing.
    fl_dock_notify_window_closed(&s_stub_host_handle); // already removed, so no second demote
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 2);
}

// The three checks on a failed promote that keep a panel from being lost.
UTEST(dock_workspace, promote_failure_paths_via_host) {
    DOCK_PROMOTE_CASE(fixture, scope, src);

    // At enqueue: promoting an id the workspace does not hold enqueues nothing, and the host is never called.
    dock_promote_and_run(src, 999);
    ASSERT_EQ_MSG(s_stub_host.open_calls, 0, "absent-panel promote is a no-op (nothing enqueued)");
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 2);

    // At flush: the panel left the source between enqueue and flush, so dock_workspace_promote fails. The second
    // workspace that was just created is rolled back and no window is opened.
    dock_promote_panel(101);
    FlDockRequest close101 = { .kind = FlDockRequest_Close, .panel = 101 };
    dock_tree_apply(src->tree, &close101); // 101 leaves src before the next begin drains the queue
    dock_run_frame(src);
    ASSERT_EQ_MSG(s_stub_host.open_calls, 0, "flush with a vanished panel opens no window (dst rolled back)");
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 1);

    // Host failure: open_promoted_window returns nullptr, so the panel is demoted straight back into the source.
    dock_workspace_set_tree(src, dock_tree_build(dock_split(FlDockAxis_X, 0.5f, dock_leaf(101), dock_leaf(102))));
    s_stub_host.return_null = true;
    dock_promote_and_run(src, 101);
    ASSERT_EQ_MSG(s_stub_host.open_calls, 1, "host was asked to open the window");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(src->tree, 101) != nullptr,
                    "host failure → 101 demoted back into the source, never lost");
    ASSERT_EQ_MSG((int)dock_tree_collect_panels(src->tree, nullptr, 0), 2, "conservation holds across a failed open");
}

// A panel a drag has in flight belongs to that gesture. Promoting it would leave the drop to dock a second copy.
UTEST(dock_workspace, a_dragged_panel_is_not_promoted) {
    DOCK_PROMOTE_CASE(fixture, scope, src);

    dock_drag_begin_undock(&src->drag, src->tree, 101, (FlDockFamilyGate) { 0 });
    dock_promote_and_run(src, 101);
    bool still_in_flight = dock_tree_panel_is_floating(src->tree, 101);
    dock_drag_discard(&src->drag); // the scope restores the tree, so the gesture only has to let go

    ASSERT_EQ_MSG(s_stub_host.open_calls, 0, "no window opens for a panel that is mid-drag");
    ASSERT_TRUE_MSG(still_in_flight, "the drag keeps the panel");
    ASSERT_EQ_MSG((int)dock_tree_collect_panels(src->tree, nullptr, 0), 2, "and the workspace still holds both");
}

// The drag previews a copy of the tree taken when it found its first target. A promote that pulled another panel
// out meanwhile would leave that panel in the copy, drawn in two windows and offered as a drop target.
UTEST(dock_workspace, a_promote_waits_for_the_drag_in_its_workspace_to_end) {
    DOCK_PROMOTE_CASE(fixture, scope, src);

    dock_drag_begin_undock(&src->drag, src->tree, 101, (FlDockFamilyGate) { 0 });
    dock_promote_and_run(src, 102);
    int opened_mid_drag = s_stub_host.open_calls;
    bool stayed_docked = dock_tree_find_leaf_with_panel(src->tree, 102) != nullptr;

    dock_drag_cancel(&src->drag, src->tree);
    dock_run_frame(src);

    ASSERT_EQ_MSG(opened_mid_drag, 0, "no window opens while the drag is in flight");
    ASSERT_TRUE_MSG(stayed_docked, "and the panel stays where the drag's preview shows it");
    ASSERT_EQ_MSG(s_stub_host.open_calls, 1, "the request is applied once the drag is over");
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src->tree, 102) == nullptr);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(s_stub_host.last_ws->tree, 102) != nullptr);
}

// A drag in another workspace holds nothing back here.
UTEST(dock_workspace, a_drag_elsewhere_does_not_hold_a_promote) {
    static int second_handle;
    DOCK_PROMOTE_CASE(fixture, scope, src);

    dock_promote_and_run(src, 101);
    ASSERT_EQ(s_stub_host.open_calls, 1);
    FlDockWorkspace* promoted = s_stub_host.last_ws;

    s_stub_host.handle = &second_handle;
    dock_drag_begin_undock(&promoted->drag, promoted->tree, 101, (FlDockFamilyGate) { 0 });
    dock_promote_and_run(src, 102);
    dock_drag_cancel(&promoted->drag, promoted->tree);

    ASSERT_EQ_MSG(s_stub_host.open_calls, 2, "the main workspace has no drag, so its promote goes through");
}

// An app may ask every frame until the panel has left. While a drag holds the request back, the copies must not
// fill the queue and push out a request for another panel.
UTEST(dock_workspace, a_repeated_promote_request_is_queued_once) {
    FlUiCtx* fixture = render_fixture_get();
    ASSERT_TRUE_MSG(fixture != nullptr, "render fixture creation failed");
    DOCK_STUB_HOST(
        scope, fixture,
        dock_split(FlDockAxis_X, 0.5f, dock_leaf(101), dock_split(FlDockAxis_Y, 0.5f, dock_leaf(102), dock_leaf(103))));
    FlDockWorkspace* src = scope.tree.ws;

    dock_drag_begin_undock(&src->drag, src->tree, 101, (FlDockFamilyGate) { 0 });
    for (int i = 0; i < FL_DOCK_MAX_WORKSPACES; ++i) {
        dock_promote_panel(102);
    }
    dock_promote_panel(103);
    dock_drag_cancel(&src->drag, src->tree);
    dock_run_frame(src);

    ASSERT_EQ_MSG(s_stub_host.open_calls, 2, "both panels get their window");
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src->tree, 102) == nullptr);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src->tree, 103) == nullptr);
}

// A host may report one window closed while it is opening another. When the closed window is the one the new
// promotion came out of, the new window's way home is that window's own source.
UTEST(dock_workspace, a_promotion_survives_its_source_closing_while_its_window_opens) {
    static int second_handle;
    DOCK_PROMOTE_CASE(fixture, scope, src);

    FlDockWorkspace* first = dock_open_first_window(src);
    ASSERT_TRUE_MSG(first != nullptr, "the first window opens and takes panel 103");

    s_stub_host.handle = &second_handle;
    s_stub_host.closes_on_open = &s_stub_host_handle;
    dock_promote_from(fixture, first, src, 103);
    s_stub_host.closes_on_open = nullptr;
    ASSERT_EQ(s_stub_host.open_calls, 2);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(src->tree, 101) != nullptr, "the first window closed");

    fl_dock_notify_window_closed(&second_handle);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(src->tree, 103) != nullptr,
                    "103 comes home to the workspace its source window came from");
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 3);
}

// The same close, from a host that then fails to open the window: the panel goes back to a workspace that still
// exists.
UTEST(dock_workspace, a_failed_open_demotes_past_a_source_that_closed_meanwhile) {
    DOCK_PROMOTE_CASE(fixture, scope, src);

    FlDockWorkspace* first = dock_open_first_window(src);
    ASSERT_TRUE_MSG(first != nullptr, "the first window opens and takes panel 103");

    s_stub_host.return_null = true;
    s_stub_host.closes_on_open = &s_stub_host_handle;
    dock_promote_from(fixture, first, src, 103);

    ASSERT_EQ(s_stub_host.open_calls, 2);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(src->tree, 103) != nullptr, "103 lands in the main workspace");
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 3);
}

// Closing a promoted window docks its panels into the source at once, because their workspace is going away. A
// drag in flight there is cancelled first, as Escape would, so its preview never misses them.
UTEST(dock_workspace, closing_a_promoted_window_cancels_a_drag_in_its_source) {
    DOCK_PROMOTE_CASE(fixture, scope, src);

    dock_promote_and_run(src, 101);
    ASSERT_EQ(s_stub_host.open_calls, 1);

    dock_drag_begin_undock(&src->drag, src->tree, 102, (FlDockFamilyGate) { 0 });
    fl_dock_notify_window_closed(&s_stub_host_handle);
    bool drag_survived = dock_drag_active(&src->drag);
    dock_drag_discard(&src->drag);

    ASSERT_FALSE_MSG(drag_survived, "the drag ends when the tree changes under it");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(src->tree, 102) != nullptr, "the dragged panel is docked again");
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src->tree, 101) != nullptr);
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 2);
    ASSERT_TRUE_MSG(src->grab_blocked, "the held button does not pick the tab straight back up");
}

// Closing a promoted window brings back everything docked in it, not only the panel it was opened for.
UTEST(dock_workspace, closing_a_promoted_window_returns_every_panel_it_holds) {
    DOCK_PROMOTE_CASE(fixture, scope, src);

    FlDockWorkspace* promoted = dock_open_first_window(src);
    ASSERT_TRUE_MSG(promoted != nullptr, "the promoted window opens and takes panel 103");

    fl_dock_notify_window_closed(&s_stub_host_handle);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src->tree, 101) != nullptr);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(src->tree, 103) != nullptr,
                    "a panel docked into the promoted window comes back with it");
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 3);
}

// A panel promoted out of a promoted window outlives the window it came from: when that one closes first, the
// panel's way home is the closed window's own source.
UTEST(dock_workspace, a_nested_promotion_survives_its_parent_window_closing) {
    static int second_handle;
    DOCK_PROMOTE_CASE(fixture, scope, src);

    FlDockWorkspace* first = dock_open_first_window(src);
    ASSERT_TRUE_MSG(first != nullptr, "the first window opens and takes panel 103");

    // Promote 103 out of the first promoted window, as that window's own frame would.
    s_stub_host.handle = &second_handle;
    dock_promote_from(fixture, first, src, 103);
    ASSERT_EQ(s_stub_host.open_calls, 2);

    fl_dock_notify_window_closed(&s_stub_host_handle); // the first window, the one 103 was promoted from
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src->tree, 101) != nullptr);
    fl_dock_notify_window_closed(&second_handle);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(src->tree, 103) != nullptr,
                    "103 comes home to the workspace its parent window came from");
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 3);
}

// The same through the real headless fl_app and window backend. The default dock host registered by fl_app_create
// opens a real headless window for the promoted subtree, and an injected window close demotes it back.
typedef struct DockPromoteE2E {
    DockFixture fx;           // fx.ws follows whichever window is mid-frame, so it is not the main workspace
    FlDockWorkspace* main_ws; // the main window's workspace (registry entry 0), stable for the app's lifetime
    bool requested;
} DockPromoteE2E;

static void dock_promote_e2e_frame(void* user_data) {
    DockPromoteE2E* st = (DockPromoteE2E*)user_data;
    FlDockWorkspace* ws = dock_fixture_emit(&st->fx, dock_split(FlDockAxis_X, 0.5f, dock_leaf(201), dock_leaf(202)));
    if (st->main_ws == nullptr) {
        st->main_ws = ws; // the promoted window does not exist yet, so the first workspace to appear is the main one
    }
    dock_workspace_end(ws);

    // Request the promote once, at the end of the main window's frame. The next dock_workspace_begin flushes it.
    if (!st->requested) {
        st->requested = true;
        dock_promote_panel(201);
    }
}

// Two windows lay out in one tick and share one id space, so a promoted window's dock nodes must not reuse the ids
// the main window's nodes have. The layout here is built through the ops, which keeps the main tree's first node
// (id 1) alive, the id a fresh tree also starts from.
typedef struct DockPromoteIds {
    bool built;
    bool requested;
} DockPromoteIds;

static void dock_promote_ids_frame(void* user_data) {
    DockPromoteIds* st = (DockPromoteIds*)user_data;
    if (!st->built) {
        st->built = true;
        fl_dock_add_panel(201);
        fl_dock_dock_outer(202, FlDockSide_Right);
    }
    fl_dock_workspace_begin();
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        fl_dock_workspace_emit();
    }
    fl_dock_workspace_end();
    if (!st->requested) {
        st->requested = true;
        fl_dock_promote_panel(202);
    }
}

UTEST(dock_workspace, a_promoted_window_shares_no_node_ids_with_the_main_one) {
    DOCK_APP(app, "dockpromoteids", 800, 600);
    DockPromoteIds st = { 0 };
    for (int i = 0; i < 5; i++) {
        fl_app_tick(app, &st, dock_promote_ids_frame); // a shared node id is fatal in the tick both windows lay out
    }
    ASSERT_EQ_MSG(fl_app_window_count(app), 2, "the promoted window is live and both windows laid out");
}

UTEST(dock_workspace, promote_open_close_demote_via_fl_app) {
    DOCK_APP(app, "dockpromote", 800, 600);
    DockPromoteE2E st = { 0 };

    // Tick 1: install the layout, emit, then enqueue the promote. Only the main window exists so far.
    fl_app_tick(app, &st, dock_promote_e2e_frame);
    ASSERT_TRUE_MSG(st.main_ws != nullptr, "main workspace captured");
    ASSERT_EQ_MSG(fl_app_window_count(app), 1, "only the main window before the promote materializes");
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(st.main_ws->tree, 201) != nullptr);

    // Tick 2: the main window's begin flushes the promote. The subtree moves to a second workspace and the host
    // queues the OS window open, so 201 has left the source but the window is not live yet.
    fl_app_tick(app, &st, dock_promote_e2e_frame);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(st.main_ws->tree, 201) == nullptr,
                    "201 promoted out of the source workspace");

    // Tick 3: the queued promoted window is created, a real headless window hosting the second workspace.
    fl_app_tick(app, &st, dock_promote_e2e_frame);
    ASSERT_EQ_MSG(fl_app_window_count(app), 2, "the promoted window is live (compositor-placed by the WM on Wayland)");

    // Close the promoted window (windows[1]). The default host's on_close demotes 201 back and removes the second
    // workspace. The next tick tears the window down.
    fl_app_inject_window_close_for(app, fl_app_window_at(app, 1));
    fl_app_tick(app, &st, dock_promote_e2e_frame);
    ASSERT_EQ_MSG(fl_app_window_count(app), 1,
                  "the promoted window closed — second workspace dissolved (registry clean)");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(st.main_ws->tree, 201) != nullptr,
                    "201 demoted back into the source workspace by stable id");
    ASSERT_EQ((int)dock_tree_collect_panels(st.main_ws->tree, nullptr, 0), 2);
}
