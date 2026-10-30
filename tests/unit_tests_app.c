// app lifecycle, window-backend and shared-ABI tests

#include "unit_tests_common.h"
#include "platform/sdl/sdl_input_common.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// multi_window.per_window_clay_context - the per-window Clay context split: each window owns a
// distinct Clay context, ui_make_current swaps both the core singleton and Clay's current context,
// render-command capture is isolated per window, and ui_destroy drops a dangling Clay current pointer - a
// stale Clay current context left by a destroy is dereferenced by the next ui_create's Clay_MinMemorySize.

UTEST(multi_window, per_window_clay_context) {
    // The fixture brings up the process-global systems (vfs, image, font) once and hands back primary window A.
    FlUiCtx* a = render_fixture_get();
    ASSERT_TRUE_MSG(a != nullptr, "render fixture creation failed");

    ui_make_current(a);
    ASSERT_TRUE_MSG(ui_get_ctx() == a, "ui_make_current must select the core singleton");
    ASSERT_TRUE_MSG(Clay_GetCurrentContext() == a->clay_ctx, "ui_make_current must select the window's Clay context");

    // A second window on the SAME shared resource tier - its own Clay context, distinct from A's.
    FlUiCtx* b = ui_create(s_arena, a->shared);
    ASSERT_TRUE_MSG(b != nullptr, "second-window ui_create failed");
    ASSERT_TRUE_MSG(b->clay_ctx != a->clay_ctx, "each window must own a distinct Clay context");

    ui_make_current(b);
    ASSERT_TRUE_MSG(ui_get_ctx() == b, "ui_make_current must reselect to B");
    ASSERT_TRUE_MSG(Clay_GetCurrentContext() == b->clay_ctx, "Clay current must follow to B's context");

    // Render-command capture is per-window: B has not run a frame, so its capture is empty.
    ASSERT_EQ_MSG(ui_get_render_command_count(), 0, "a window that has not run a frame has no render commands");

    ui_make_current(a);
    a->layout_width = 256.0f;
    a->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_update_begin(a);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = ui_color(200, 40, 50, 255)) {
    }
    ui_update_end(a, 1.0f / 60.0f);
    ASSERT_TRUE_MSG(ui_get_render_command_count() > 0, "A's frame must capture render commands on A");

    ui_make_current(b);
    ASSERT_EQ_MSG(ui_get_render_command_count(), 0, "B's render-command capture is isolated from A's frame");

    ui_destroy(b);
    ASSERT_TRUE_MSG(Clay_GetCurrentContext() == nullptr,
                    "ui_destroy must null Clay's current context when it names the destroyed window");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window_backend - the headless WindowBackend seam. Constructs the backend directly (no fl_app) and drives its
// per-window seams: lifecycle, drawable size + content scale, and the cursor / hidden-drag state the core
// flushes each frame.

UTEST(window_backend, headless_seam) {
    FlWindowBackend wb = headless_window_backend_create(s_arena);
    ASSERT_TRUE_MSG(wb.create_window != nullptr, "headless window backend exposes the seam");

    FlWindowConfig cfg = { .title = FL_S("headless"), .width = 640, .height = 480 };
    FlWindow* w = wb.create_window(wb.user_data, &cfg);
    ASSERT_TRUE_MSG(w != nullptr, "headless window creation failed");

    int dw = 0, dh = 0;
    wb.drawable_size(wb.user_data, w, &dw, &dh);
    ASSERT_EQ_MSG(dw, 640, "drawable width tracks the window config");
    ASSERT_EQ_MSG(dh, 480, "drawable height tracks the window config");
    ASSERT_TRUE_MSG(wb.content_scale(wb.user_data, w) == 1.0f, "headless content scale is DPI-neutral 1.0");

    wb.set_cursor(wb.user_data, w, (FlCursor) { .shape = FlCursorShape_Crosshair });
    wb.set_cursor(wb.user_data, w, (FlCursor) { .shape = FlCursorShape_Hand });
    ASSERT_EQ_MSG(window_backend_headless_last_cursor(&wb).shape, FlCursorShape_Hand,
                  "the last set_cursor of the frame wins and is recorded");
    wb.set_cursor_visible(wb.user_data, w, false);
    ASSERT_FALSE_MSG(window_backend_headless_cursor_visible(&wb), "set_cursor_visible(false) is recorded");
    wb.set_cursor_visible(wb.user_data, w, true);
    ASSERT_TRUE_MSG(window_backend_headless_cursor_visible(&wb), "set_cursor_visible(true) is recorded");

    ASSERT_FALSE_MSG(window_backend_headless_relative_mouse_mode(&wb), "relative mode starts disabled");
    wb.set_relative_mouse_mode(wb.user_data, w, true);
    ASSERT_TRUE_MSG(window_backend_headless_relative_mouse_mode(&wb), "set_relative_mouse_mode(true) is recorded");
    wb.warp_mouse(wb.user_data, w, 12.5f, 34.0f);
    FlVec2 warp = window_backend_headless_last_warp(&wb);
    ASSERT_TRUE_MSG(warp.x == 12.5f && warp.y == 34.0f, "warp_mouse records the last warp target");

    wb.destroy_window(wb.user_data, w);
    wb.destroy(wb.user_data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// main window config - settings width/height are logical pixels scaled by the backend's pre-window display content
// scale; the headless backend reports none, so it stays 1.0. fixed_size reaches the backend config inverted as
// resizable.

static float stub_display_scale_1_5(void* ud) {
    UNUSED(ud);
    return 1.5f;
}

UTEST(window_backend, main_window_config_scales_logical_size) {
    FlAppSettings settings = { .window_title = FL_S("scaled"), .width = 801, .height = 600 };

    FlWindowBackend headless = headless_window_backend_create(s_arena);
    FlWindowConfig cfg = fl_app_main_window_config(&settings, &headless);
    ASSERT_EQ_MSG(cfg.width, 801, "headless keeps scale 1.0: width is the logical width");
    ASSERT_EQ_MSG(cfg.height, 600, "headless keeps scale 1.0: height is the logical height");
    headless.destroy(headless.user_data);

    FlWindowBackend scaled = { .display_content_scale = stub_display_scale_1_5 };
    cfg = fl_app_main_window_config(&settings, &scaled);
    ASSERT_EQ_MSG(cfg.width, 1202, "801 * 1.5 rounds to the nearest pixel");
    ASSERT_EQ_MSG(cfg.height, 900, "600 * 1.5");
}

UTEST(window_backend, main_window_config_carries_resizable) {
    FlWindowBackend backend = { 0 };
    FlAppSettings settings = { .window_title = FL_S("default"), .width = 100, .height = 100 };
    ASSERT_TRUE_MSG(fl_app_main_window_config(&settings, &backend).resizable,
                    "a zero-initialised struct stays resizable");
    settings.fixed_size = true;
    ASSERT_FALSE_MSG(fl_app_main_window_config(&settings, &backend).resizable, "fixed_size reaches the config");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window_backend - two inject_text calls before a single pump must each queue their own text. Each queued text
// event owns its backing bytes; a shared per-window buffer would let the second inject overwrite the first
// event's text before the pump drains it. The queued events are read back directly so a clobbered first event
// is caught even though the drained end state concatenates both.

UTEST(window_backend, headless_inject_text_back_to_back_keeps_distinct_text) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlWindowBackend wb = headless_window_backend_create(s_arena);
    ASSERT_TRUE_MSG(wb.create_window != nullptr, "headless window backend creation failed");
    FlWindowConfig cfg = { .title = FL_S("inject_text"), .width = 320, .height = 240 };
    FlWindow* w = wb.create_window(wb.user_data, &cfg);
    ASSERT_TRUE_MSG(w != nullptr, "headless window creation failed");

    // Back-to-back injects, no pump in between.
    window_backend_headless_inject_text(&wb, w, FL_S("first"));
    window_backend_headless_inject_text(&wb, w, FL_S("second"));

    FlString q0 = window_backend_headless_queued_text(w, 0);
    FlString q1 = window_backend_headless_queued_text(w, 1);
    ASSERT_TRUE_MSG(q0.length == 5 && memcmp(q0.data, "first", 5) == 0,
                    "the first queued text keeps its own bytes after a second inject");
    ASSERT_TRUE_MSG(q1.length == 6 && memcmp(q1.data, "second", 6) == 0, "the second queued text is intact");
    ASSERT_TRUE_MSG(q0.data != q1.data, "each queued text event owns distinct backing storage");

    // Drain both into the input pipeline (the app pumps before ui_update_begin, so no frame is open here): the
    // pump copies each event's text out synchronously and appends it, so the raw per-pump kb field holds both
    // strings in order.
    FlWindow* windows[1] = { w };
    FlUiCtx* contexts[1] = { ctx };
    wb.pump_events(wb.user_data, windows, contexts, 1);
    FlString delivered = ctx->input_state.keyboard.text_input;
    ASSERT_TRUE_MSG(delivered.length == 11 && memcmp(delivered.data, "firstsecond", 11) == 0,
                    "the pump delivers every injected text to the input pipeline, in order");

    wb.destroy_window(wb.user_data, w);
    wb.destroy(wb.user_data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Text-input events within one pump are appended in order and reach the per-frame snapshot; a lone event is
// delivered unchanged; text past the cap keeps the earliest characters and never splits a UTF-8 codepoint.

static FlString typed_snapshot_after_pushing(FlUiCtx* ctx, const FlString* events, u32 count) {
    for_count(i, count) {
        fl_platform_input_push_text_input_event(ctx, (FlTextInputEvent) { .text = events[i] });
    }
    fl_platform_input_update(ctx, 1.0f / 60.0f);
    return ctx->text_input_this_frame;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(input, text_input_events_in_one_pump_concatenate) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlString two[] = { FL_S("a"), FL_S("b") };
    FlString got = typed_snapshot_after_pushing(ctx, two, 2);
    ASSERT_TRUE_MSG(got.length == 2 && memcmp(got.data, "ab", 2) == 0, "both events land in the snapshot, in order");

    FlString one[] = { FL_S("x") };
    got = typed_snapshot_after_pushing(ctx, one, 1);
    ASSERT_TRUE_MSG(got.length == 1 && got.data[0] == 'x', "a single event is delivered unchanged");

    got = typed_snapshot_after_pushing(ctx, one, 0);
    ASSERT_EQ_MSG((int)got.length, 0, "the next frame starts with an empty snapshot");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(input, text_input_overflow_keeps_the_earliest_text) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    char fill[UI_TEXT_INPUT_THIS_FRAME_CAP - 1];
    memset(fill, 'a', sizeof(fill));
    FlString events[] = { (FlString) { .data = fill, .length = sizeof(fill) }, FL_S("bc"), FL_S("d") };
    FlString got = typed_snapshot_after_pushing(ctx, events, 3);
    ASSERT_EQ_MSG((int)got.length, UI_TEXT_INPUT_THIS_FRAME_CAP, "the snapshot is clamped at the cap");
    ASSERT_TRUE_MSG(got.data[0] == 'a' && got.data[got.length - 1] == 'b',
                    "the earliest characters survive and the overflowing tail is dropped");

    // A two-byte codepoint that straddles the cap is dropped whole.
    FlString utf8[] = { (FlString) { .data = fill, .length = sizeof(fill) }, FL_S("\xC3\xA9") };
    got = typed_snapshot_after_pushing(ctx, utf8, 2);
    ASSERT_EQ_MSG((int)got.length, (int)sizeof(fill), "a codepoint that does not fit is not split");
    typed_snapshot_after_pushing(ctx, utf8, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The per-frame string helpers resolve their arena through the process-global UI context. With no frame open there
// is no arena, so the precondition check is fatal in every build type rather than log-and-continue.

#if defined(__linux__)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum FrameStrDeathCall {
    FrameStrDeathCall_Str,
    FrameStrDeathCall_Array,
} FrameStrDeathCall;

static int frame_str_no_frame_child_status(FrameStrDeathCall call) {
    fflush(nullptr); // so the child does not inherit, and later re-emit, the parent's pending stdio
    pid_t pid = fork();
    if (pid == 0) {
        // The child is expected to abort. A zero core limit keeps the system core handler from writing a dump
        // for it, and from stalling the run for the second or so that takes. Its streams go to /dev/null so the
        // expected FATAL assert line does not read as a crash in an otherwise passing run.
        setrlimit(RLIMIT_CORE, &(struct rlimit) { .rlim_cur = 0, .rlim_max = 0 });
        (void)freopen("/dev/null", "w", stdout);
        (void)freopen("/dev/null", "w", stderr);
        if (call == FrameStrDeathCall_Str) {
            fl_frame_str("hi", 2);
        } else {
            fl_frame_str_array(3);
        }
        _exit(0); // reached only if the precondition failed to fire
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}

// Whether the child terminated instead of running to the clean _exit(0). abort() (a signal) and exit(1) both count,
// which is what lets one assertion cover the debug and release assert handlers alike.
static bool frame_str_child_terminated(int status) {
    return !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
#endif

UTEST(app, frame_str_outside_a_frame_terminates) {
#if defined(__linux__)
    ASSERT_TRUE_MSG(frame_str_child_terminated(frame_str_no_frame_child_status(FrameStrDeathCall_Str)),
                    "fl_frame_str returned instead of terminating with no frame open");
    ASSERT_TRUE_MSG(frame_str_child_terminated(frame_str_no_frame_child_status(FrameStrDeathCall_Array)),
                    "fl_frame_str_array returned instead of terminating with no frame open");
#else
    UTEST_SKIP("fork-based death test is Linux-only");
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A pointer/keyboard edge pumped before a tick's event poll must be visible to that SAME tick's frame() body. The
// per-frame edge snapshot (fl_platform_input_update, which derives mouse_pressed_this_frame from the pumped
// buttons_pressed and then clears it) has to run AFTER fl_app_pump_events and BEFORE the frame callback.
typedef struct InputLatState {
    int tick;              // bumped at the top of each frame() call (1-based)
    int pressed_seen_tick; // first tick whose frame() observed mouse_pressed_this_frame (-1 = never)
} InputLatState;

static void input_lat_frame(void* user_data) {
    InputLatState* st = (InputLatState*)user_data;
    st->tick++;
    if (ui_get_ctx()->mouse_pressed_this_frame && st->pressed_seen_tick < 0) {
        st->pressed_seen_tick = st->tick;
    }
    // Minimal layout so ui_update_end has a tree to walk.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {}
}

UTEST(app, input_edge_latency) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("lat"), .width = 64, .height = 64, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    InputLatState st = { .tick = 0, .pressed_seen_tick = -1 };

    // Queue a left-button press BEFORE the first tick's event pump: a tail-of-tick snapshot would defer it to tick 2.
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);

    fl_app_tick(app, &st, input_lat_frame); // tick 1
    fl_app_tick(app, &st, input_lat_frame); // tick 2 (so a deferred edge is still observed, just late)

    ASSERT_EQ_MSG(st.pressed_seen_tick, 1,
                  "press pumped before tick 1 must reach tick-1 frame(); a value of 2 means the edge snapshot "
                  "runs after the frame callback and every click is a frame late");

    fl_app_destroy(app);
}

// A tab strip commits the new selection on mouse-DOWN, without waiting for the release.
typedef struct TabPressState {
    int tab;
    FlUiCtx* ctx; // captured during the tick so we can read laid-out tab geometry afterwards
} TabPressState;

static void tab_press_frame(void* user_data) {
    TabPressState* st = (TabPressState*)user_data;
    st->ctx = ui_get_ctx();
    FlString tabs[3] = { FL_S("Gen"), FL_S("Aud"), FL_S("Vid") };
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_tab_bar(tabs, (int)(sizeof(tabs) / sizeof(tabs[0])), &st->tab);
    }
}

UTEST(app, tab_bar_activates_on_press) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("tabpress"), .width = 400, .height = 200, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    TabPressState st = { .tab = 0, .ctx = nullptr };

    // The tab id mirrors ui_tab_bar_impl: instance id = (u32)(uintptr_t)selected_index, folded with the tab index
    // via ui_item_id_seed.
    fl_app_tick(app, &st, tab_press_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");

    u32 tab1_id = ui_layout_id_to_clay_id(
        (FlLayoutId) { .name = FL_S("tab"), .id = ui_item_id_seed((u32)(uintptr_t)&st.tab, 1u) });
    ItemState* tab1 = hashmap_get(&st.ctx->shared->item_state, tab1_id);
    ASSERT_TRUE_MSG(tab1 != nullptr && tab1->state.size.x > 0.0f, "second tab was not laid out");
    i32 cx = (i32)(tab1->state.pos.x + tab1->state.size.x * 0.5f);
    i32 cy = (i32)(tab1->state.pos.y + tab1->state.size.y * 0.5f);

    // Hover the second tab for a couple of frames so hot state propagates (next_hot_id -> hot_id -> state.hot).
    fl_app_inject_mouse_move(app, cx, cy);
    fl_app_tick(app, &st, tab_press_frame);
    fl_app_tick(app, &st, tab_press_frame);
    ASSERT_EQ_MSG(st.tab, 0, "hovering a tab must not change the selection");

    // Press and DO NOT release.
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, tab_press_frame);
    ASSERT_EQ_MSG(st.tab, 1, "tab bar must select on press (mouse-down), without waiting for release");

    fl_app_destroy(app);
}

// fl_app owns the UI-scale policy the core does not: it pushes monitor content scale x ui_zoom to ui_set_scale on
// init, on resize, and whenever the zoom changes. The headless display backend reports a content scale of 1.0, so
// the composed scale is exactly the ui_zoom.

// A single full-window GROW root painted a solid color so it emits one rectangle covering the whole layout area.
// Plain non-interactive layout areas are not tracked in item_state, so the laid-out size is read from the
// render-command snapshot instead; the rect width is physical pixels, so its logical width is width / ui_scale.
static const FlColor SCALE_ROOT_COLOR = { 30, 90, 160, 255 };

static void scale_root_frame(void* user_data) {
    (void)user_data;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = SCALE_ROOT_COLOR) {}
}

// Width (physical px) of the widest rectangle in the current render-command snapshot, or -1 if none.
static f32 scale_root_rect_width(void) {
    f32 widest = -1.0f;
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || cmd.type != UiRenderCmd_Rectangle) {
            continue;
        }
        if (cmd.width > widest) {
            widest = cmd.width;
        }
    }
    return widest;
}

// The default is DPI-correct (content 1.0 x zoom 1.0 = 1.0), and fl_app_set_ui_zoom recomposes the absolute scale
// immediately and uniformly (ui_scale == text_scale == zoom), surviving subsequent ticks.
UTEST(app, ui_zoom_recomposes_scale) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("zoom"), .width = 400, .height = 300, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    fl_app_tick(app, nullptr, scale_root_frame);
    ASSERT_TRUE_MSG(ui_get_scale() == 1.0f, "default composed UI scale is DPI-correct (1.0)");
    ASSERT_TRUE_MSG(ui_get_text_scale() == 1.0f, "default composed text scale is DPI-correct (1.0)");

    fl_app_set_ui_zoom(app, 1.5f);
    ASSERT_TRUE_MSG(ui_get_scale() == 1.5f, "fl_app_set_ui_zoom recomposes the UI scale at once");
    ASSERT_TRUE_MSG(ui_get_text_scale() == 1.5f, "zoom enlarges the UI uniformly (text scale tracks ui scale)");

    // The per-tick recompose dedups on the unchanged product.
    fl_app_tick(app, nullptr, scale_root_frame);
    ASSERT_TRUE_MSG(ui_get_scale() == 1.5f, "a steady tick keeps the composed scale");

    fl_app_set_ui_zoom(app, 2.0f);
    ASSERT_TRUE_MSG(ui_get_scale() == 2.0f, "raising the zoom raises the scale");
    fl_app_set_ui_zoom(app, 1.0f);
    ASSERT_TRUE_MSG(ui_get_scale() == 1.0f, "returning the zoom to 1.0 is DPI-correct again");

    fl_app_destroy(app);
}

// At a fixed DPI a bigger window yields proportionally more logical layout area; raising the zoom on the same
// window enlarges the UI uniformly without changing the logical content count.
UTEST(app, window_size_drives_logical_area) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings small = { .window_title = FL_S("small"), .width = 400, .height = 300, .headless = true };
    FlApp* app_small = fl_app_create(&small);
    ASSERT_TRUE_MSG(app_small != nullptr, "fl_app_create(small) failed");
    fl_app_tick(app_small, nullptr, scale_root_frame);
    float physical_small = scale_root_rect_width();
    ASSERT_TRUE_MSG(physical_small > 0.0f, "small: the GROW root was laid out");
    float logical_small = physical_small / ui_get_scale();
    ASSERT_NEAR_MSG((double)logical_small, 400.0, 1.0, "small: logical area equals the window width at scale 1.0");
    fl_app_destroy(app_small);

    FlAppSettings big = { .window_title = FL_S("big"), .width = 800, .height = 600, .headless = true };
    FlApp* app_big = fl_app_create(&big);
    ASSERT_TRUE_MSG(app_big != nullptr, "fl_app_create(big) failed");
    fl_app_tick(app_big, nullptr, scale_root_frame);
    float physical_big = scale_root_rect_width();
    ASSERT_TRUE_MSG(physical_big > 0.0f, "big: the GROW root was laid out");
    float logical_big = physical_big / ui_get_scale();
    ASSERT_NEAR_MSG((double)logical_big, 2.0 * (double)logical_small, 2.0,
                    "a bigger window at fixed DPI yields proportionally more logical layout area");

    // Zoom x2 stands in for a 2x-DPI display of the same window: the physical rect is unchanged because the window
    // did not change, while the logical content count drops back to the small window's.
    fl_app_set_ui_zoom(app_big, 2.0f);
    fl_app_tick(app_big, nullptr, scale_root_frame);
    float physical_zoom = scale_root_rect_width();
    ASSERT_TRUE_MSG(physical_zoom > 0.0f, "zoom: the GROW root was laid out");
    ASSERT_NEAR_MSG((double)physical_zoom, (double)physical_big, 2.0,
                    "zoom does not change the window: the rendered rect stays the same size");
    float logical_zoom = physical_zoom / ui_get_scale();
    ASSERT_NEAR_MSG((double)logical_zoom, (double)logical_small, 2.0,
                    "a 2x zoom halves the logical area: same content count as the small window, bigger UI");
    fl_app_destroy(app_big);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Multi-window. The headless window backend gives each window its own injected-event queue, so these drive the
// app-level contract end-to-end.

typedef struct WinFrameCount {
    int frames;
} WinFrameCount;

static void win_frame_count(void* user_data) {
    WinFrameCount* c = (WinFrameCount*)user_data;
    c->frames++;
    // Minimal layout so ui_update_end has a tree to walk.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {}
}

UTEST(app, multi_window_open_demux_close) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("main"), .width = 200, .height = 150, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    WinFrameCount main_c = { 0 };
    WinFrameCount sec_c = { 0 };

    FlWindowSettings ws = { .title = FL_S("second"),
                            .width = 160,
                            .height = 120,
                            .x = FL_APP_WINDOW_POS_DEFAULT,
                            .y = FL_APP_WINDOW_POS_DEFAULT };
    FlAppWindow* second = fl_app_window_open(app, &ws, win_frame_count, &sec_c);
    ASSERT_TRUE_MSG(second != nullptr, "fl_app_window_open failed");

    // One tick runs BOTH windows' frame callbacks: the main window's is the one passed to fl_app_tick, the
    // second's is the callback it was opened with - each against its own user data.
    ASSERT_TRUE_MSG(fl_app_tick(app, &main_c, win_frame_count), "tick with two windows keeps running");
    ASSERT_EQ_MSG(main_c.frames, 1, "main window's frame ran this tick");
    ASSERT_EQ_MSG(sec_c.frames, 1, "second window's frame ran this tick (its own callback + user data)");

    fl_app_inject_window_close_for(app, second);
    ASSERT_TRUE_MSG(fl_app_tick(app, &main_c, win_frame_count), "closing a non-main window leaves the app running");
    ASSERT_EQ_MSG(main_c.frames, 2, "main window keeps ticking after the secondary closed");
    ASSERT_EQ_MSG(sec_c.frames, 1, "closed secondary window's frame no longer runs (the close hit only it)");

    fl_app_inject_window_close(app);
    ASSERT_FALSE_MSG(fl_app_tick(app, &main_c, win_frame_count), "closing the main window quits the app");

    fl_app_destroy(app);
}

UTEST(app, multi_window_explicit_close) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("main"), .width = 120, .height = 90, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    WinFrameCount main_c = { 0 };
    WinFrameCount sec_c = { 0 };

    FlWindowSettings ws = { .title = FL_S("second"),
                            .width = 100,
                            .height = 80,
                            .x = FL_APP_WINDOW_POS_DEFAULT,
                            .y = FL_APP_WINDOW_POS_DEFAULT };
    FlAppWindow* second = fl_app_window_open(app, &ws, win_frame_count, &sec_c);
    ASSERT_TRUE_MSG(second != nullptr, "fl_app_window_open failed");

    ASSERT_TRUE_MSG(fl_app_tick(app, &main_c, win_frame_count), "first tick keeps running");
    ASSERT_EQ_MSG(sec_c.frames, 1, "second window ran before the explicit close");

    // fl_app_window_close requests close; the window is torn down at the start of the next tick (the same deferred
    // path a window-manager close takes), so the app keeps running and only the main window renders afterward.
    fl_app_window_close(app, second);
    ASSERT_TRUE_MSG(fl_app_tick(app, &main_c, win_frame_count), "explicit close of a non-main window keeps running");
    ASSERT_EQ_MSG(main_c.frames, 2, "main window keeps ticking after fl_app_window_close");
    ASSERT_EQ_MSG(sec_c.frames, 1, "explicitly closed window's frame no longer runs");

    // Quit is the main window's close through fl_app_tick, so this request is a no-op.
    fl_app_window_close(app, fl_app_main_window(app));
    ASSERT_TRUE_MSG(fl_app_tick(app, &main_c, win_frame_count), "fl_app_window_close on the main window is a no-op");
    ASSERT_EQ_MSG(main_c.frames, 3, "main window still ticking after a no-op close request");

    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared item state is retired once per app tick, after every window has refreshed its own records, so one
// window's cleanup never drops another live window's items.

typedef struct ItemRetireProbe {
    bool emit_extra; // main window also emits the item that later disappears
    FlUiCtx* ctx;
} ItemRetireProbe;

static void retire_item(FlLayoutId id) {
    ui_layout_area_id(id, .sizing = { sizing_fixed(20.0f), sizing_fixed(20.0f) }, .color = ui_color(200, 40, 50, 255)) {
        ui_item_behavior(id, FlItemFlags_clickable, (FlWidgetConfig) { 0 });
    }
}

static void item_retire_frame_main(void* user_data) {
    ItemRetireProbe* p = (ItemRetireProbe*)user_data;
    p->ctx = ui_get_ctx();
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        retire_item(ID("retire_item_main"));
        if (p->emit_extra) {
            retire_item(ID("retire_item_extra"));
        }
    }
}

static void item_retire_frame_sec(void* user_data) {
    (void)user_data;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        retire_item(ID("retire_item_sec"));
    }
}

static const ItemState* item_retire_lookup(FlUiCtx* ctx, FlLayoutId id) {
    return hashmap_get(&ctx->shared->item_state, ui_layout_id_to_clay_id(id));
}

UTEST(app, multi_window_item_state_survives_cross_window_cleanup) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("main"), .width = 200, .height = 150, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    ItemRetireProbe main_p = { .emit_extra = true };

    // The main window advances alone first, so the window opened afterwards starts from a later epoch.
    fl_app_tick(app, &main_p, item_retire_frame_main);
    fl_app_tick(app, &main_p, item_retire_frame_main);
    ASSERT_TRUE_MSG(main_p.ctx != nullptr, "main frame did not run");

    FlWindowSettings ws = { .title = FL_S("second"),
                            .width = 160,
                            .height = 120,
                            .x = FL_APP_WINDOW_POS_DEFAULT,
                            .y = FL_APP_WINDOW_POS_DEFAULT };
    ASSERT_TRUE_MSG(fl_app_window_open(app, &ws, item_retire_frame_sec, nullptr) != nullptr,
                    "fl_app_window_open failed");

    for (int i = 0; i < 4; i++) {
        fl_app_tick(app, &main_p, item_retire_frame_main);

        const ItemState* main_item = item_retire_lookup(main_p.ctx, ID("retire_item_main"));
        const ItemState* sec_item = item_retire_lookup(main_p.ctx, ID("retire_item_sec"));
        ASSERT_TRUE_MSG(main_item != nullptr && main_item->state.found, "main window's record was erased");
        ASSERT_TRUE_MSG(main_item->state.size.x > 0.0f, "main window's previous-frame geometry was lost");
        if (i > 0) {
            ASSERT_TRUE_MSG(sec_item != nullptr && sec_item->state.found, "second window's record was erased");
            ASSERT_TRUE_MSG(sec_item->state.size.x > 0.0f, "second window's previous-frame geometry was lost");
        }
    }

    ASSERT_TRUE_MSG(item_retire_lookup(main_p.ctx, ID("retire_item_extra")) != nullptr, "extra item should be live");

    // The extra item stops being emitted: it is retired after the complete tick, the live ones are kept.
    main_p.emit_extra = false;
    fl_app_tick(app, &main_p, item_retire_frame_main);
    ASSERT_TRUE_MSG(item_retire_lookup(main_p.ctx, ID("retire_item_extra")) == nullptr,
                    "a disappeared element's record must be retired after the app frame");
    ASSERT_TRUE_MSG(item_retire_lookup(main_p.ctx, ID("retire_item_main")) != nullptr, "main record kept");
    ASSERT_TRUE_MSG(item_retire_lookup(main_p.ctx, ID("retire_item_sec")) != nullptr, "second record kept");

    fl_app_destroy(app);
}

UTEST(app, standalone_frame_prunes_disappeared_items) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_make_current(ctx);
    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    for (int i = 0; i < 2; i++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            retire_item(ID("standalone_item_a"));
            retire_item(ID("standalone_item_b"));
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }
    ASSERT_TRUE_MSG(item_retire_lookup(ctx, ID("standalone_item_a")) != nullptr, "item a should be live");
    ASSERT_TRUE_MSG(item_retire_lookup(ctx, ID("standalone_item_b")) != nullptr, "item b should be live");

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        retire_item(ID("standalone_item_a"));
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_TRUE_MSG(item_retire_lookup(ctx, ID("standalone_item_a")) != nullptr, "item a must survive");
    ASSERT_TRUE_MSG(item_retire_lookup(ctx, ID("standalone_item_b")) == nullptr,
                    "item b disappeared and must be pruned by the standalone frame path");
}

#ifndef NDEBUG
// The debug duplicate-id tracker is app-global: it accumulates every window's ids across one app tick rather than
// being wiped per-window. A true cross-window collision trips a fatal assert and so cannot be asserted in-process;
// distinct-id accumulation across windows is the observable proxy.
typedef struct DupIdProbe {
    FlUiCtx* ctx; // captured during the frame so the test reads THIS app's shared tier unambiguously
} DupIdProbe;

static void dup_id_frame_a(void* user_data) {
    ((DupIdProbe*)user_data)->ctx = ui_get_ctx();
    ui_layout_area_id(ID("dup_id_win_a"), .sizing = { .width = sizing_grow(), .height = sizing_grow() }) {}
}

static void dup_id_frame_b(void* user_data) {
    ((DupIdProbe*)user_data)->ctx = ui_get_ctx();
    ui_layout_area_id(ID("dup_id_win_b"), .sizing = { .width = sizing_grow(), .height = sizing_grow() }) {}
}

UTEST(app, multi_window_dup_id_tracker_is_app_global) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("main"), .width = 200, .height = 150, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    DupIdProbe main_p = { 0 };
    DupIdProbe sec_p = { 0 };

    FlWindowSettings ws = { .title = FL_S("second"),
                            .width = 160,
                            .height = 120,
                            .x = FL_APP_WINDOW_POS_DEFAULT,
                            .y = FL_APP_WINDOW_POS_DEFAULT };
    FlAppWindow* second = fl_app_window_open(app, &ws, dup_id_frame_b, &sec_p);
    ASSERT_TRUE_MSG(second != nullptr, "fl_app_window_open failed");

    // One tick runs both windows. The tracker is cleared once at the tick boundary, then both windows emit into it.
    ASSERT_TRUE_MSG(fl_app_tick(app, &main_p, dup_id_frame_a), "tick with two windows keeps running");
    ASSERT_TRUE_MSG(main_p.ctx != nullptr && sec_p.ctx != nullptr, "both window frames ran and captured a context");

    UiShared* shared = main_p.ctx->shared;
    ASSERT_TRUE_MSG(shared == sec_p.ctx->shared, "both windows share the one app-global resource tier");

    const u32 id_a = ui_layout_id_to_clay_id(ID("dup_id_win_a"));
    const u32 id_b = ui_layout_id_to_clay_id(ID("dup_id_win_b"));
    ASSERT_TRUE_MSG(hashmap_get(&shared->debug_id_tracker, id_a) != nullptr,
                    "tracker retains window A's id after the tick");
    ASSERT_TRUE_MSG(hashmap_get(&shared->debug_id_tracker, id_b) != nullptr,
                    "tracker retains window B's id from the SAME tick (a per-window clear would have wiped it)");
    ASSERT_EQ_MSG((int)hashmap_count(&shared->debug_id_tracker), 2,
                  "tracker holds exactly both windows' ids — accumulated across windows, not cleared per-window");

    ASSERT_TRUE_MSG(fl_app_tick(app, &main_p, dup_id_frame_a),
                    "second tick re-emits the same ids without tripping the duplicate assert");
    ASSERT_EQ_MSG((int)hashmap_count(&shared->debug_id_tracker), 2,
                  "tracker is cleared once per tick, then re-accumulates both windows (not doubled)");

    fl_app_destroy(app);
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Multi-window lifecycle hardening. Mid-frame open is queued and applied at the next tick boundary (never
// mid-emit), and an optional on_close hook can veto a close (default policy: main close quits).

// Opens a second window from INSIDE its own emit. The open must be queued, not executed mid-frame, which would
// mutate the window list and swap the current context out from under this very emit.
typedef struct MidFrameOpen {
    FlApp* app;
    FlAppFrameFn sec_frame;
    void* sec_user_data;
    FlAppWindow* opened;
    int main_frames;
    bool requested;
} MidFrameOpen;

static void mid_frame_open_main(void* user_data) {
    MidFrameOpen* s = (MidFrameOpen*)user_data;
    s->main_frames++;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {}
    if (!s->requested) {
        s->requested = true;
        FlWindowSettings ws = { .title = FL_S("detached"),
                                .width = 100,
                                .height = 80,
                                .x = FL_APP_WINDOW_POS_DEFAULT,
                                .y = FL_APP_WINDOW_POS_DEFAULT };
        s->opened = fl_app_window_open(s->app, &ws, s->sec_frame, s->sec_user_data);
    }
}

UTEST(app, multi_window_mid_frame_open_deferred) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("main"), .width = 160, .height = 120, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    WinFrameCount sec_c = { 0 };
    MidFrameOpen st = { .app = app, .sec_frame = win_frame_count, .sec_user_data = &sec_c };

    // Tick 1: the main frame opens a window mid-emit. The handle is returned immediately, but the window is QUEUED.
    ASSERT_TRUE_MSG(fl_app_tick(app, &st, mid_frame_open_main), "tick with a mid-frame open keeps running");
    ASSERT_TRUE_MSG(st.opened != nullptr, "fl_app_window_open returns a live handle immediately, even when queued");
    ASSERT_EQ_MSG(st.main_frames, 1, "main window emitted once");
    ASSERT_EQ_MSG(sec_c.frames, 0, "the mid-frame-opened window did NOT run during the emit that opened it");

    ASSERT_TRUE_MSG(fl_app_tick(app, &st, mid_frame_open_main), "second tick keeps running");
    ASSERT_EQ_MSG(sec_c.frames, 1, "the queued window is live the next tick (materialized at the boundary)");
    ASSERT_EQ_MSG(st.main_frames, 2, "main window keeps emitting");

    // The materialized window closes like any other (proves it really joined the live list).
    fl_app_inject_window_close_for(app, st.opened);
    ASSERT_TRUE_MSG(fl_app_tick(app, &st, mid_frame_open_main), "closing the detached window leaves the app running");
    ASSERT_EQ_MSG(sec_c.frames, 1, "the closed detached window no longer runs");

    fl_app_destroy(app);
}

UTEST(app, multi_window_mid_frame_open_cancelled) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("main"), .width = 160, .height = 120, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    WinFrameCount sec_c = { 0 };
    MidFrameOpen st = { .app = app, .sec_frame = win_frame_count, .sec_user_data = &sec_c };

    ASSERT_TRUE_MSG(fl_app_tick(app, &st, mid_frame_open_main), "tick with a mid-frame open keeps running");
    ASSERT_TRUE_MSG(st.opened != nullptr, "queued open returns a handle");
    ASSERT_EQ_MSG(sec_c.frames, 0, "queued window has not run yet");

    // Close the still-pending handle BEFORE the next tick materializes it: the queued open is cancelled.
    fl_app_window_close(app, st.opened);

    // The main frame won't re-open - st.requested is latched.
    ASSERT_TRUE_MSG(fl_app_tick(app, &st, mid_frame_open_main), "second tick keeps running after the cancel");
    ASSERT_EQ_MSG(sec_c.frames, 0, "a queued open cancelled before materialization never comes up");
    ASSERT_EQ_MSG(st.main_frames, 2, "main window keeps emitting");

    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Multi-window per-window correctness: per-window DPI/scale composition, per-window keyboard focus restored on
// activation, and a hidden drag that keeps its OS mouse-grab when the cursor leaves its window.

// Two windows on different-DPI monitors each compose THEIR OWN absolute UI scale = own content scale x the app-wide
// ui_zoom. Each frame callback runs with its window current, so ui_get_scale() reads that window's scale.
typedef struct WinScaleProbe {
    float scale;
} WinScaleProbe;

static void win_scale_probe_frame(void* user_data) {
    WinScaleProbe* p = (WinScaleProbe*)user_data;
    p->scale = ui_get_scale(); // current context == this window during its tick slice
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {}
}

UTEST(app, multi_window_per_window_scale) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("main"), .width = 200, .height = 150, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    WinScaleProbe main_p = { 0 };
    WinScaleProbe sec_p = { 0 };

    FlWindowSettings ws = { .title = FL_S("second"),
                            .width = 160,
                            .height = 120,
                            .x = FL_APP_WINDOW_POS_DEFAULT,
                            .y = FL_APP_WINDOW_POS_DEFAULT };
    FlAppWindow* second = fl_app_window_open(app, &ws, win_scale_probe_frame, &sec_p);
    ASSERT_TRUE_MSG(second != nullptr, "fl_app_window_open failed");
    FlAppWindow* main_win = fl_app_main_window(app);

    // Distinct per-window content scales (the offscreen stand-in for two different-DPI monitors).
    fl_app_set_window_content_scale_for(app, main_win, 1.0f);
    fl_app_set_window_content_scale_for(app, second, 2.0f);

    fl_app_tick(app, &main_p, win_scale_probe_frame);
    ASSERT_TRUE_MSG(main_p.scale == 1.0f, "main composes its own content scale (1.0) x zoom (1.0)");
    ASSERT_TRUE_MSG(sec_p.scale == 2.0f, "second composes its OWN content scale (2.0), not the main window's");

    // The app-wide UI-zoom multiplies each window's own content scale (composition is per-window).
    fl_app_set_ui_zoom(app, 1.5f);
    fl_app_tick(app, &main_p, win_scale_probe_frame);
    ASSERT_TRUE_MSG(main_p.scale == 1.5f, "main: content 1.0 x zoom 1.5 = 1.5");
    ASSERT_TRUE_MSG(sec_p.scale == 3.0f, "second: content 2.0 x zoom 1.5 = 3.0 (per-window, not clobbered)");

    fl_app_destroy(app);
}

// Keyboard focus is per-window (each window's FlUiCtx holds its own focused_id) and restored on activation:
// deactivating a window clears its focus ring; reactivating it restores the exact field it had. The probe snapshots
// focused_id at the top of the frame, i.e. after fl_platform_input_update has applied that tick's activation edge.
typedef struct FocusProbe {
    bool set_once;  // request focus on the next frame
    u32 focused_id; // focused_id snapshot at frame start
} FocusProbe;

static void focus_frame_main(void* user_data) {
    FocusProbe* p = (FocusProbe*)user_data;
    p->focused_id = ui_get_ctx()->focused_id;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_item_behavior(ID("focus_item_main"), FlItemFlags_focusable, (FlWidgetConfig) { 0 });
    }
    if (p->set_once) {
        fl_set_focus(ID("focus_item_main"));
        p->set_once = false;
    }
}

static void focus_frame_sec(void* user_data) {
    FocusProbe* p = (FocusProbe*)user_data;
    p->focused_id = ui_get_ctx()->focused_id;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_item_behavior(ID("focus_item_sec"), FlItemFlags_focusable, (FlWidgetConfig) { 0 });
    }
    if (p->set_once) {
        fl_set_focus(ID("focus_item_sec"));
        p->set_once = false;
    }
}

UTEST(app, multi_window_focus_restore) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("main"), .width = 200, .height = 150, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FocusProbe main_p = { .set_once = true };
    FocusProbe sec_p = { .set_once = true };

    FlWindowSettings ws = { .title = FL_S("second"),
                            .width = 160,
                            .height = 120,
                            .x = FL_APP_WINDOW_POS_DEFAULT,
                            .y = FL_APP_WINDOW_POS_DEFAULT };
    FlAppWindow* second = fl_app_window_open(app, &ws, focus_frame_sec, &sec_p);
    ASSERT_TRUE_MSG(second != nullptr, "fl_app_window_open failed");
    FlAppWindow* main_win = fl_app_main_window(app);

    const u32 main_id = ui_layout_id_to_clay_id(ID("focus_item_main"));
    const u32 sec_id = ui_layout_id_to_clay_id(ID("focus_item_sec"));

    // Tick 1 sets focus in each window; tick 2 observes it persisted, per-window and distinct.
    fl_app_tick(app, &main_p, focus_frame_main);
    fl_app_tick(app, &main_p, focus_frame_main);
    ASSERT_EQ_MSG(main_p.focused_id, main_id, "main window holds its own focused widget");
    ASSERT_EQ_MSG(sec_p.focused_id, sec_id, "second window holds its OWN focused widget (focus is per-window)");

    fl_app_inject_window_focus_for(app, main_win, /*gained*/ false);
    fl_app_tick(app, &main_p, focus_frame_main);
    ASSERT_EQ_MSG(main_p.focused_id, (u32)0, "a deactivated window shows no focus (cleared, not auto-re-picked)");
    ASSERT_EQ_MSG(sec_p.focused_id, sec_id,
                  "the still-active window's focus is unaffected by the other's deactivation");

    fl_app_tick(app, &main_p, focus_frame_main);
    ASSERT_EQ_MSG(main_p.focused_id, (u32)0, "focus stays cleared while the window is inactive");

    fl_app_inject_window_focus_for(app, main_win, /*gained*/ true);
    fl_app_tick(app, &main_p, focus_frame_main);
    ASSERT_EQ_MSG(main_p.focused_id, main_id, "activation restores the window's own previously-focused widget");
    ASSERT_EQ_MSG(sec_p.focused_id, sec_id, "the second window's focus never moved through any of this");

    fl_app_destroy(app);
}

// A hidden drag started in one window holds the OS mouse-grab (relative mode) for its whole duration even though
// the other window ticks afterwards with no drag of its own: the grab is app-global state owned by the dragging
// window, never clobbered by the last window ticked. On drag end the cursor warps back to the press point in the
// owning window.
typedef struct DragProbe {
    bool begin_once;   // begin a hidden drag on the next frame
    bool end_once;     // end the hidden drag on the next frame
    float scale;       // hidden-drag sensitivity
    float accum_x;     // accumulated scaled drag delta
    bool relative_now; // this window's relative-mode flag this frame
} DragProbe;

static void drag_frame_main(void* user_data) {
    DragProbe* p = (DragProbe*)user_data;
    if (p->begin_once) {
        ui_mouse_hidden_drag_begin(p->scale);
        p->begin_once = false;
    }
    FlVec2 d = ui_mouse_drag_delta();
    p->accum_x += d.x;
    if (p->end_once) {
        ui_mouse_hidden_drag_end();
        p->end_once = false;
    }
    p->relative_now = ui_get_relative_mouse_mode();
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {}
}

static void drag_frame_sec(void* user_data) {
    DragProbe* p = (DragProbe*)user_data;
    (void)ui_mouse_drag_delta();
    p->relative_now = ui_get_relative_mouse_mode();
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {}
}

UTEST(app, multi_window_hidden_drag_capture) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("main"), .width = 200, .height = 150, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    DragProbe main_p = { .begin_once = true, .scale = 2.0f };
    DragProbe sec_p = { 0 };

    FlWindowSettings ws = { .title = FL_S("second"),
                            .width = 160,
                            .height = 120,
                            .x = FL_APP_WINDOW_POS_DEFAULT,
                            .y = FL_APP_WINDOW_POS_DEFAULT };
    FlAppWindow* second = fl_app_window_open(app, &ws, drag_frame_sec, &sec_p);
    ASSERT_TRUE_MSG(second != nullptr, "fl_app_window_open failed");

    // Place the cursor at a known point so the drag's press point (and thus the warp-back target) is deterministic
    // and non-zero - distinguishing "warped back" from the (0,0) initial readback.
    fl_app_inject_mouse_move(app, 50, 60);

    // The main window begins a hidden drag; the second window ticks AFTER it with no drag of its own.
    fl_app_tick(app, &main_p, drag_frame_main);
    ASSERT_TRUE_MSG(main_p.relative_now, "the dragging window is in relative mode");
    ASSERT_FALSE_MSG(sec_p.relative_now, "the non-dragging window is not in relative mode");
    ASSERT_TRUE_MSG(fl_app_headless_relative_mouse_mode(app),
                    "the OS mouse-grab survives the non-dragging window's tick (not clobbered to false)");

    // A raw delta injected into the main window is read by its drag, scaled, so the grab keeps feeding deltas to
    // the originating window.
    fl_app_inject_mouse_delta(app, 5.0f, 0.0f);
    fl_app_tick(app, &main_p, drag_frame_main);
    ASSERT_NEAR_MSG((double)main_p.accum_x, 10.0, 0.001, "the dragging window reads the scaled delta (5 x 2.0)");
    ASSERT_TRUE_MSG(fl_app_headless_relative_mouse_mode(app), "the grab still holds mid-drag");

    main_p.end_once = true;
    fl_app_tick(app, &main_p, drag_frame_main);
    ASSERT_FALSE_MSG(fl_app_headless_relative_mouse_mode(app), "ending the drag releases the OS mouse-grab");
    FlVec2 warp = fl_app_headless_last_warp(app);
    ASSERT_TRUE_MSG(warp.x == 50.0f && warp.y == 60.0f, "drag end warps the cursor back to the press point");

    fl_app_destroy(app);
}

// on_close hooks: one always vetoes (close blocked), one always approves. The call counter proves the hook is
// actually consulted on each close path.
static bool on_close_veto(void* user_data) {
    int* calls = (int*)user_data;
    (*calls)++;
    return false; // veto every close
}

static bool on_close_allow(void* user_data) {
    int* calls = (int*)user_data;
    (*calls)++;
    return true; // approve every close
}

UTEST(app, on_close_veto_keeps_secondary) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("main"), .width = 120, .height = 90, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    WinFrameCount main_c = { 0 };
    WinFrameCount sec_c = { 0 };
    FlWindowSettings ws = { .title = FL_S("second"),
                            .width = 100,
                            .height = 80,
                            .x = FL_APP_WINDOW_POS_DEFAULT,
                            .y = FL_APP_WINDOW_POS_DEFAULT };
    FlAppWindow* second = fl_app_window_open(app, &ws, win_frame_count, &sec_c);
    ASSERT_TRUE_MSG(second != nullptr, "fl_app_window_open failed");

    int veto_calls = 0;
    fl_app_window_set_on_close(app, second, on_close_veto, &veto_calls);

    ASSERT_TRUE_MSG(fl_app_tick(app, &main_c, win_frame_count), "first tick keeps running");
    ASSERT_EQ_MSG(sec_c.frames, 1, "second window ran before any close");

    fl_app_inject_window_close_for(app, second);
    ASSERT_TRUE_MSG(fl_app_tick(app, &main_c, win_frame_count), "vetoed close leaves the app running");
    ASSERT_EQ_MSG(veto_calls, 1, "on_close consulted on the close");
    ASSERT_EQ_MSG(sec_c.frames, 2, "vetoed window still ticked this frame (the close was blocked)");

    // The veto cleared the sticky flag, so a fresh close re-consults the hook.
    fl_app_inject_window_close_for(app, second);
    ASSERT_TRUE_MSG(fl_app_tick(app, &main_c, win_frame_count), "second vetoed close still keeps running");
    ASSERT_EQ_MSG(veto_calls, 2, "on_close consulted again on the next close");
    ASSERT_EQ_MSG(sec_c.frames, 3, "still alive after a second veto");

    fl_app_window_set_on_close(app, second, on_close_allow, &veto_calls);
    fl_app_inject_window_close_for(app, second);
    ASSERT_TRUE_MSG(fl_app_tick(app, &main_c, win_frame_count), "approved secondary close keeps the app running");
    ASSERT_EQ_MSG(veto_calls, 3, "approving hook consulted");
    ASSERT_EQ_MSG(sec_c.frames, 3, "approved-closed window no longer ticks");

    fl_app_destroy(app);
}

UTEST(app, on_close_veto_blocks_main_quit) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("main"), .width = 120, .height = 90, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    WinFrameCount main_c = { 0 };
    int calls = 0;
    fl_app_window_set_on_close(app, fl_app_main_window(app), on_close_veto, &calls);

    fl_app_inject_window_close(app);
    ASSERT_TRUE_MSG(fl_app_tick(app, &main_c, win_frame_count), "vetoed main-window close does NOT quit");
    ASSERT_EQ_MSG(calls, 1, "main on_close consulted");
    ASSERT_EQ_MSG(main_c.frames, 1, "main window still ticking after the veto");

    fl_app_window_set_on_close(app, fl_app_main_window(app), on_close_allow, &calls);
    fl_app_inject_window_close(app);
    ASSERT_FALSE_MSG(fl_app_tick(app, &main_c, win_frame_count), "approved main-window close quits the app");
    ASSERT_EQ_MSG(calls, 2, "approving hook consulted on the quit");

    fl_app_destroy(app);
}

UTEST(app, on_close_clear_restores_default_policy) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("main"), .width = 120, .height = 90, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    WinFrameCount main_c = { 0 };
    WinFrameCount sec_c = { 0 };
    FlWindowSettings ws = { .title = FL_S("second"),
                            .width = 100,
                            .height = 80,
                            .x = FL_APP_WINDOW_POS_DEFAULT,
                            .y = FL_APP_WINDOW_POS_DEFAULT };
    FlAppWindow* second = fl_app_window_open(app, &ws, win_frame_count, &sec_c);
    ASSERT_TRUE_MSG(second != nullptr, "fl_app_window_open failed");

    int calls = 0;
    fl_app_window_set_on_close(app, second, on_close_veto, &calls);
    ASSERT_TRUE_MSG(fl_app_tick(app, &main_c, win_frame_count), "first tick keeps running");

    fl_app_inject_window_close_for(app, second);
    ASSERT_TRUE_MSG(fl_app_tick(app, &main_c, win_frame_count), "vetoed close keeps the app running");
    ASSERT_EQ_MSG(calls, 1, "hook consulted while installed");
    ASSERT_EQ_MSG(sec_c.frames, 2, "vetoed window still alive");

    // Clearing the hook (null) restores the default policy.
    fl_app_window_set_on_close(app, second, nullptr, nullptr);
    fl_app_inject_window_close_for(app, second);
    ASSERT_TRUE_MSG(fl_app_tick(app, &main_c, win_frame_count), "default-policy close of a secondary keeps running");
    ASSERT_EQ_MSG(calls, 1, "cleared hook is no longer consulted");
    ASSERT_EQ_MSG(sec_c.frames, 2, "default policy closed the secondary (it no longer ticks)");

    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// direct_ui_calls - the flowi UI surface is a set of plain exported `fl_*` functions; plugins link the
// shared library and call them directly rather than through a composed service vtable.

int external_c_variadic_abi_probe(void);

int external_c_frame_format_abi_probe(void);

int external_c_shared_library_smoke(const char* library_path, int (*const* in_frame_probes)(void),
                                    int in_frame_probe_count);

int external_cpp_abi_probe(void);

int external_cpp_frame_format_abi_probe(void);

UTEST(shared_abi, external_c_and_cpp_consumers_link_and_call_public_symbols) {
    ASSERT_TRUE_MSG(external_c_variadic_abi_probe(), "external C variadic calls failed");
    ASSERT_TRUE_MSG(external_cpp_abi_probe(), "external C++ linkage/call failed");
}

UTEST(shared_abi, headless_app_renders_through_public_shared_library) {
    // The frame-arena formatter reads the UI context of whichever flowi instance is running the frame, and
    // unit_tests keeps its statically linked internals out of .dynsym so the loaded library binds to its own. The
    // probes therefore run inside the shared library's frame rather than around a frame begun in the executable.
    static int (*const in_frame_probes[])(void)
        = { external_c_frame_format_abi_probe, external_cpp_frame_format_abi_probe };
    ASSERT_TRUE_MSG(external_c_shared_library_smoke(FLOWI_SHARED_LIBRARY_PATH, in_frame_probes,
                                                    (int)(sizeof(in_frame_probes) / sizeof(in_frame_probes[0]))),
                    "public-only consumer could not render, format a frame string, and read back a frame "
                    "through libflowi");
}

#if BACKEND_USE_SDL

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// sdl_input.* - SDL event translation driven through the real pump with a virtual controller and synthetic events.

static int sdl_input_connected_count(const FlUiCtx* ctx) {
    int count = 0;
    for (int i = 0; i < UI_INPUT_MAX_GAMEPADS; i++) {
        count += ctx->input_state.gamepads[i].connected ? 1 : 0;
    }
    return count;
}

UTEST(sdl_input, startup_gamepad_reports_connected_once) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ASSERT_EQ(SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER), 0);
    const int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0);
    ASSERT_TRUE_MSG(device >= 0, "virtual joystick unavailable");
    ASSERT_TRUE_MSG(SDL_IsGameController(device), "virtual joystick must map as a game controller");

    for (int i = 0; i < UI_INPUT_MAX_GAMEPADS; i++) {
        ctx->input_state.gamepads[i].connected = false;
    }

    SdlInputCommon input;
    sdl_input_common_init(&input);
    FlUiCtx* contexts[1] = { ctx };
    const u32 window_ids[1] = { 0 };

    sdl_input_common_pump_multi(&input, window_ids, contexts, 1);
    EXPECT_EQ(sdl_input_connected_count(ctx), 1);

    // SDL's own queued ADDED for the same controller must not report it a second time.
    for (int i = 0; i < UI_INPUT_MAX_GAMEPADS; i++) {
        ctx->input_state.gamepads[i].connected = false;
    }
    sdl_input_common_pump_multi(&input, window_ids, contexts, 1);
    EXPECT_EQ(sdl_input_connected_count(ctx), 0);

    sdl_input_common_destroy(&input);
    SDL_JoystickDetachVirtual(device);
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    SDL_QuitSubSystem(SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool sdl_input_push_mouse_button(u32 type, u8 button) {
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.button.button = button;
    e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    e.button.clicks = 1;
    return SDL_PushEvent(&e) == 1;
}

UTEST(sdl_input, unmapped_mouse_button_produces_no_event) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ASSERT_EQ(SDL_InitSubSystem(SDL_INIT_EVENTS), 0);
    SdlInputCommon input;
    sdl_input_common_init(&input);
    FlUiCtx* contexts[1] = { ctx };
    const u32 window_ids[1] = { 0 };
    MouseState* mouse = &ctx->input_state.mouse;
    memset(mouse->buttons, 0, sizeof(mouse->buttons));
    memset(mouse->buttons_pressed, 0, sizeof(mouse->buttons_pressed));
    memset(mouse->buttons_released, 0, sizeof(mouse->buttons_released));

    ASSERT_TRUE(sdl_input_push_mouse_button(SDL_MOUSEBUTTONDOWN, 6));
    ASSERT_TRUE(sdl_input_push_mouse_button(SDL_MOUSEBUTTONUP, 6));
    sdl_input_common_pump_multi(&input, window_ids, contexts, 1);
    for (int i = 0; i < FlMouseButton_Count; i++) {
        EXPECT_FALSE(mouse->buttons[i]);
        EXPECT_FALSE(mouse->buttons_pressed[i]);
        EXPECT_FALSE(mouse->buttons_released[i]);
    }

    ASSERT_TRUE(sdl_input_push_mouse_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT));
    sdl_input_common_pump_multi(&input, window_ids, contexts, 1);
    EXPECT_TRUE(mouse->buttons[FlMouseButton_Left]);
    mouse->buttons[FlMouseButton_Left] = false;
    mouse->buttons_pressed[FlMouseButton_Left] = false;

    sdl_input_common_destroy(&input);
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

#endif // BACKEND_USE_SDL
