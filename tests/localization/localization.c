// Localization loading and language switching tests

#include <math.h>

// SID_ display text resolved against the default language's string table, loaded from the checked-in example
// manifest under examples/data/localization.

#include "localization/common.h"

UTEST(localization, sid_renders_the_default_table_translation) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    char snapshot[8192];
    localization_emit(ctx, FL_S("loc_probe"), FL_S("SID_settings_title"), SNAPSHOT_BUFFER(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "Settings") != nullptr, "the SID must render as its default-table text");

    // Non-SID text is never looked up, so it reaches the tree byte-for-byte.
    localization_emit(ctx, FL_S("loc_probe"), FL_S("Plain label"), SNAPSHOT_BUFFER(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "Plain label") != nullptr, "non-SID text passes through untouched");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, missing_key_renders_the_sid_literally) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(ctx->shared->localization, FL_S("SID_not_in_the_table")),
                                     FL_S("SID_not_in_the_table")),
                    "a key with no entry resolves to the key itself");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_plural_key_on_the_implicit_path_resolves_the_other_variant) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    // The implicit path has no count, so the key resolves to its "other" variant with the placeholder left
    // visible, and not to the SID literal.
    ASSERT_TRUE_MSG(
        fl_string_equals(localization_lookup(ctx->shared->localization, FL_S("SID_file_count")), FL_S("{0} files")),
        "a plural key with no count in hand renders its other variant");

    char snapshot[8192];
    localization_emit(ctx, FL_S("loc_probe"), FL_S("SID_file_count"), SNAPSHOT_BUFFER(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "{0} files") != nullptr, "the widget path renders the same other variant");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, unreadable_manifest_disables_localization) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("no_such_manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    char snapshot[8192];
    localization_emit(ctx, FL_S("loc_probe"), FL_S("SID_settings_title"), SNAPSHOT_BUFFER(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "SID_settings_title") != nullptr, "with no manifest every SID renders as itself");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, widget_identity_is_the_raw_sid) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    char snapshot[8192];
    localization_emit(ctx, FL_S("SID_settings_title"), FL_S("SID_settings_title"), SNAPSHOT_BUFFER(snapshot));

    FlInspectorHit hit;
    ASSERT_TRUE_MSG(fl_inspector_hit_test_name(FL_S("SID_settings_title"), &hit),
                    "the element is still named by the raw SID after its text was translated");

    const u32 sid_id = ui_layout_id_to_clay_id(resolve_widget_id(FL_S("SID_settings_title"), string_empty()));
    const u32 translated_id = ui_layout_id_to_clay_id(resolve_widget_id(FL_S("Settings"), string_empty()));
    ASSERT_NE_MSG(sid_id, translated_id, "the id must not follow the display text");
    ASSERT_EQ_MSG(sid_id, hit.id, "the emitted element carries the raw SID's id, not the translation's");

    localization_fixture_release(ctx, mount);
}

// ui_app_frame_begin is where the startup barrier runs, so this case drives fl_app_tick.
static void localization_barrier_frame(void* user_data) {
    (void)user_data;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_text(FL_S("SID_settings_title"));
    }
}

UTEST(localization, startup_never_blocks_and_text_resolves_once_the_table_lands) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlVfsMount* mount = localization_mount();
    ASSERT_TRUE_MSG(mount != nullptr, "mounting the localization example data failed");

    FlAppSettings settings = { .window_title = FL_S("loc"), .width = 256, .height = 256, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlUiCtx* ctx = g_ui_ctx;
    fl_localization_init(mount, FL_S("manifest.json"), false);

    fl_app_tick(app, nullptr, localization_barrier_frame);

    // The tree is captured when the frame ends, so it is read here rather than inside the callback.
    char snapshot[8192];
    int32_t needed = fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));

    // The check below is for an absence, which a truncated buffer would also satisfy.
    ASSERT_TRUE_MSG(needed > 0 && needed < (int32_t)sizeof(snapshot), "snapshot fits the buffer");
    // Whether the manifest and table have landed by the first frame is a race with the async read,
    // so the frame is only required to have rendered the SID one way or the other.
    ASSERT_TRUE_MSG(strstr(snapshot, "SID_settings_title") != nullptr || strstr(snapshot, "Settings") != nullptr,
                    "the first frame renders without waiting on the manifest or table read");

    bool resolved = false;
    for (u32 i = 0; i < 1000 && !resolved; i++) {
        fl_app_tick(app, nullptr, localization_barrier_frame);
        needed = fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
        ASSERT_TRUE_MSG(needed > 0 && needed < (int32_t)sizeof(snapshot), "snapshot fits the buffer");
        resolved = strstr(snapshot, "Settings") != nullptr;
        ASSERT_TRUE_MSG(resolved || strstr(snapshot, "SID_settings_title") != nullptr,
                        "until the table lands the key keeps rendering as itself");
    }
    ASSERT_TRUE_MSG(resolved, "the table lands within a bounded number of frames and the text resolves");
    ASSERT_TRUE_MSG(strstr(snapshot, "SID_settings_title") == nullptr,
                    "the frame the table lands on shows the translation, not the key");

    localization_fixture_release(ctx, mount);
    fl_app_destroy(app);
}

UTEST(localization, unparseable_manifest_disables_localization) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // A manifest that reads but is not JSON takes the parse-failure branch, which is separate from the
    // file-not-found branch above.
    FlArena* arena = arena_new();
    FlString dir_path = file_build_path_os(arena, get_current_directory_os(arena), FL_S("unit_test_localization_tmp"));
    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));

    const u8 broken[] = "{ this is not json";
    FlString manifest_path = file_build_path_os(arena, dir_path, FL_S("manifest.json"));
    ASSERT_EQ((int)sizeof(broken) - 1, (int)file_write_os(manifest_path, broken, (i64)sizeof(broken) - 1));

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(dir_path);
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "the scratch directory should mount through LocalFS");
    vfs_wait_all();

    fl_localization_init(mount.mount, FL_S("manifest.json"), false);
    localization_settle(ctx->shared->localization);

    char snapshot[8192];
    localization_emit(ctx, FL_S("loc_probe"), FL_S("SID_settings_title"), SNAPSHOT_BUFFER(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "SID_settings_title") != nullptr,
                    "an unparseable manifest must leave every SID rendering as itself");

    localization_fixture_release(ctx, mount.mount);
    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

// Requests a Language Switch from between two SID emissions, so a frame that applied the switch while rendering
// would come out with one key in each language.
static bool s_localization_switch_requested;

static void localization_switch_frame(void* user_data) {
    (void)user_data;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_text(FL_S("SID_settings_title"));
        if (!s_localization_switch_requested) {
            fl_localization_set_language(FL_S("de"));
            s_localization_switch_requested = true;
        }
        ui_text(FL_S("SID_greeting"));
    }
}

static bool snapshot_has(const char* snapshot, const char* text) {
    return strstr(snapshot, text) != nullptr;
}

static bool snapshot_fits(int32_t needed, size_t size) {
    return needed > 0 && needed < (int32_t)size;
}

static bool frame_is_old_language(const char* snapshot) {
    return snapshot_has(snapshot, "Settings") && snapshot_has(snapshot, "Hello,");
}

static bool frame_has_new_language_text(const char* snapshot) {
    return snapshot_has(snapshot, "Einstellungen") || snapshot_has(snapshot, "Hallo,");
}

static bool frame_has_old_language_text(const char* snapshot) {
    return snapshot_has(snapshot, "Settings") || snapshot_has(snapshot, "Hello,");
}

// Every frame renders entirely in one language: the new one on the frame that flips, the old one before it.
static void localization_check_switch_frame(int* utest_result, const char* snapshot, bool* switched) {
    *switched = snapshot_has(snapshot, "Einstellungen");
    if (*switched) {
        ASSERT_TRUE_MSG(snapshot_has(snapshot, "Hallo,"),
                        "the frame that switches renders entirely in the new language");
        ASSERT_FALSE_MSG(frame_has_old_language_text(snapshot), "nothing is left over from the old language");
    } else {
        ASSERT_TRUE_MSG(frame_is_old_language(snapshot) && !snapshot_has(snapshot, "Hallo,"),
                        "a frame with the switch still in flight renders entirely in the old language");
    }
}

UTEST(localization, a_switch_applies_at_the_frame_boundary) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlVfsMount* mount = localization_mount();
    ASSERT_TRUE_MSG(mount != nullptr, "mounting the localization example data failed");

    FlAppSettings settings = { .window_title = FL_S("locswitch"), .width = 256, .height = 256, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlUiCtx* ctx = g_ui_ctx;
    fl_localization_init(mount, FL_S("manifest.json"), false);
    localization_settle(ctx->shared->localization);
    s_localization_switch_requested = false;

    char snapshot[8192];
    fl_app_tick(app, nullptr, localization_switch_frame);
    int32_t needed = fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
    ASSERT_TRUE_MSG(snapshot_fits(needed, sizeof(snapshot)), "snapshot fits the buffer");
    ASSERT_TRUE_MSG(frame_is_old_language(snapshot),
                    "the frame that requested the switch renders entirely in the old language");
    ASSERT_FALSE_MSG(frame_has_new_language_text(snapshot), "no part of that frame may already be in the new language");

    // The table loads on a worker, so the swap lands at whichever later boundary finds it ready. Every frame up
    // to that one is entirely in the old language, and the one that flips is entirely in the new.
    bool switched = false;
    for (u32 i = 0; i < 1000 && !switched; i++) {
        fl_app_tick(app, nullptr, localization_switch_frame);
        needed = fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
        ASSERT_TRUE_MSG(snapshot_fits(needed, sizeof(snapshot)), "snapshot fits the buffer");
        localization_check_switch_frame(utest_result, snapshot, &switched);
        RETURN_IF_TEST_FAILED();
    }
    ASSERT_TRUE_MSG(switched, "the switch must land within a bounded number of frames");

    localization_fixture_release(ctx, mount);
    fl_app_destroy(app);
}

UTEST(localization, set_language_during_setup_selects_the_initial_language) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlVfsMount* mount = localization_mount();
    ASSERT_TRUE_MSG(mount != nullptr, "mounting the localization example data failed");

    FlAppSettings settings = { .window_title = FL_S("locinit"), .width = 256, .height = 256, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlUiCtx* ctx = g_ui_ctx;
    fl_localization_init(mount, FL_S("manifest.json"), false);
    fl_localization_set_language(FL_S("de"));

    // The tag is recorded before localization is even ready, so it waits for the manifest and default table and
    // then applies; the ticks in between show the key itself or the default language, never a mix.
    char snapshot[8192];
    bool selected = false;
    for (u32 i = 0; i < 1000 && !selected; i++) {
        fl_app_tick(app, nullptr, localization_barrier_frame);
        const int32_t needed = fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
        ASSERT_TRUE_MSG(needed > 0 && needed < (int32_t)sizeof(snapshot), "snapshot fits the buffer");
        selected = strstr(snapshot, "Einstellungen") != nullptr;
    }
    ASSERT_TRUE_MSG(selected, "a tag set before the first tick selects the initial language once it lands");
    ASSERT_TRUE_MSG(strstr(snapshot, "Settings") == nullptr && strstr(snapshot, "SID_settings_title") == nullptr,
                    "the frame it lands on is entirely in the requested language");

    localization_fixture_release(ctx, mount);
    fl_app_destroy(app);
}

UTEST(localization, a_key_missing_from_the_active_table_falls_back_to_the_default) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    fl_localization_set_language(FL_S("de"));
    localization_settle(loc);

    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Einstellungen")),
                    "a key the active table carries resolves from it");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_untranslated")), FL_S("Untranslated")),
                    "a key only the default table carries resolves from the fallback layer");
    ASSERT_TRUE_MSG(
        fl_string_equals(localization_lookup(loc, FL_S("SID_not_in_the_table")), FL_S("SID_not_in_the_table")),
        "a key neither table carries renders as the key itself");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_switch_retires_the_replaced_table_and_keeps_the_default_resident) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc), "the default language alone is resident at startup");

    fl_localization_set_language(FL_S("de"));
    localization_settle(loc);
    ASSERT_EQ_MSG((u32)2, localization_resident_table_count(loc),
                  "a non-default language adds its table beside the default");

    fl_localization_set_language(FL_S("pt-br"));
    localization_settle(loc);
    ASSERT_EQ_MSG((u32)2, localization_resident_table_count(loc),
                  "the table the switch replaced is retired rather than accumulated");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Configurações")),
                    "the newly loaded table is the one lookups resolve against");

    // Also covers case-insensitive tag matching: the manifest spells this one "en".
    fl_localization_set_language(FL_S("EN"));
    localization_settle(loc);
    ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc),
                  "switching back to the default leaves it as the only resident table");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("Settings")),
                    "the default is active again");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_failed_switch_keeps_the_current_language) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    fl_localization_set_language(FL_S("de"));
    localization_settle(loc);

    fl_localization_set_language(FL_S("zz-not-a-language"));
    localization_settle(loc);

    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Einstellungen")),
                    "a tag the manifest does not list leaves the active language untouched");
    ASSERT_EQ_MSG((u32)2, localization_resident_table_count(loc), "the failed load leaves no table behind");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, an_in_flight_switch_keeps_the_old_language_until_it_lands) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    fl_localization_set_language(FL_S("de"));

    u32 frames = 0;
    for (; frames < 100000; frames++) {
        localization_frame_begin(loc);
        if (!localization_is_loading(loc)) {
            break;
        }
        ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("Settings")),
                        "a frame with the table still loading resolves entirely against the old language");
        ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc),
                      "a table still being built on the worker is not resident");
        sleep_us(200);
    }

    ASSERT_TRUE_MSG(frames > 0, "the read is started at a frame boundary, so at least one frame sees it in flight");
    ASSERT_TRUE_MSG(frames < 100000, "the switch must land within a bounded number of frames");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Einstellungen")),
                    "the boundary that finds the table ready is the one that swaps it in");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_tag_set_while_a_switch_is_in_flight_is_applied_after_it) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    fl_localization_set_language(FL_S("de"));
    localization_frame_begin(loc);
    ASSERT_TRUE_MSG(localization_is_loading(loc), "the boundary that starts the read leaves it in flight");

    fl_localization_set_language(FL_S("pt-br"));

    bool landed = false;
    for (u32 i = 0; i < 100000 && !landed; i++) {
        localization_frame_begin(loc);
        const FlString text = localization_lookup(loc, FL_S("SID_settings_title"));
        ASSERT_TRUE_MSG(!fl_string_equals(text, FL_S_UTF8("Configurações")),
                        "a tag set during a switch never overtakes the table already loading");
        landed = fl_string_equals(text, FL_S_UTF8("Einstellungen"));
        sleep_us(200);
    }
    ASSERT_TRUE_MSG(landed, "the switch that was in flight is the one that lands first");

    localization_settle(loc);
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Configurações")),
                    "the tag queued behind it is applied once the first switch is done, not dropped");
    ASSERT_EQ_MSG((u32)2, localization_resident_table_count(loc),
                  "the table the queued switch replaced is retired rather than accumulated");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_switch_still_in_flight_at_teardown_is_waited_out) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    fl_localization_set_language(FL_S("de"));
    localization_frame_begin(loc);
    ASSERT_TRUE_MSG(localization_is_loading(loc), "the boundary that starts the read leaves it in flight");

    // Teardown waits for the worker before releasing the arena it may still be writing into. A failure here shows
    // as a hang or a use-after-free.
    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_table_the_worker_cannot_build_keeps_the_current_language) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Both worker-side failure modes behind a manifest that is itself fine: a table file that is not JSON, and one
    // the manifest names but that does not exist.
    FlArena* arena = arena_new();
    FlString dir_path
        = file_build_path_os(arena, get_current_directory_os(arena), FL_S("unit_test_localization_badtable"));
    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));

    const u8 manifest[] = "{\"version\":1,\"default\":\"en\","
                          "\"languages\":[{\"tag\":\"en\",\"file\":\"en.json\"},"
                          "{\"tag\":\"broken\",\"file\":\"broken.json\"},"
                          "{\"tag\":\"gone\",\"file\":\"gone.json\"}]}";
    FlString manifest_path = file_build_path_os(arena, dir_path, FL_S("manifest.json"));
    ASSERT_EQ((int)sizeof(manifest) - 1, (int)file_write_os(manifest_path, manifest, (i64)sizeof(manifest) - 1));

    const u8 en[] = "{\"version\":1,\"strings\":{\"SID_settings_title\":\"Settings\"}}";
    FlString en_path = file_build_path_os(arena, dir_path, FL_S("en.json"));
    ASSERT_EQ((int)sizeof(en) - 1, (int)file_write_os(en_path, en, (i64)sizeof(en) - 1));

    const u8 broken[] = "{ this is not json";
    FlString broken_path = file_build_path_os(arena, dir_path, FL_S("broken.json"));
    ASSERT_EQ((int)sizeof(broken) - 1, (int)file_write_os(broken_path, broken, (i64)sizeof(broken) - 1));

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(dir_path);
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "the scratch directory should mount through LocalFS");
    vfs_wait_all();

    fl_localization_init(mount.mount, FL_S("manifest.json"), false);
    struct Localization* loc = ctx->shared->localization;
    localization_frame_begin(loc);

    fl_localization_set_language(FL_S("broken"));
    localization_settle(loc);
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("Settings")),
                    "a table the worker cannot parse leaves the active language untouched");
    ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc), "the half-built table is retired, not kept");

    fl_localization_set_language(FL_S("gone"));
    localization_settle(loc);
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("Settings")),
                    "a table file the manifest names but that is missing leaves the active language untouched");
    ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc), "a failed read leaves no table behind");

    localization_fixture_release(ctx, mount.mount);
    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

// One clickable, focusable element labelled by a SID, sized and placed so the mouse the caller parks at (100, 20)
// lands inside it.
static void localization_interactive_emit(FlUiCtx* ctx) {
    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(resolve_widget_id(FL_S("SID_settings_title"), string_empty()),
                          .sizing = { sizing_fixed(200), sizing_fixed(40) }, .color = ui_color(40, 200, 60, 255)) {
            ui_item_behavior(resolve_widget_id(FL_S("SID_settings_title"), string_empty()),
                             FlItemFlags_clickable | FlItemFlags_focusable, (FlWidgetConfig) { 0 });
            ui_text(FL_S("SID_settings_title"));
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(localization, hover_and_focus_survive_a_language_switch) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const u32 sid_id = ui_layout_id_to_clay_id(resolve_widget_id(FL_S("SID_settings_title"), string_empty()));
    ctx->input_state.mouse.x = 100.0f;
    ctx->input_state.mouse.y = 20.0f;

    localization_interactive_emit(ctx);
    fl_set_focus_id(sid_id);
    localization_interactive_emit(ctx);
    ASSERT_EQ_MSG(sid_id, ctx->hot_id, "the element under the cursor is hot before the switch");
    ASSERT_EQ_MSG(sid_id, fl_focused_id(), "the element holds focus before the switch");

    fl_localization_set_language(FL_S("de"));
    localization_settle(ctx->shared->localization);
    localization_interactive_emit(ctx);

    char snapshot[8192];
    fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "Einstellungen") != nullptr, "the switch did reach the display text");
    ASSERT_EQ_MSG(sid_id, ctx->hot_id, "the id hashes the raw SID, so hover outlives the switch");
    ASSERT_EQ_MSG(sid_id, fl_focused_id(), "and so does focus: the focus ring never sees the translation");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, an_unusable_tag_is_rejected_and_the_last_tag_set_wins) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    fl_localization_set_language(string_empty());
    // 64 characters, one over what the pending-tag buffer holds.
    fl_localization_set_language(FL_S("0123456789012345678901234567890123456789012345678901234567890123"));
    localization_settle(loc);
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("Settings")),
                    "a tag that is empty or too long is rejected outright, never recorded as pending");
    ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc), "a rejected tag loads nothing");

    // There is one pending slot and no queue, so only the last tag set before the boundary is applied.
    fl_localization_set_language(FL_S("pt-br"));
    fl_localization_set_language(FL_S("de"));
    localization_settle(loc);
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Einstellungen")),
                    "the last tag set before the frame boundary is the one that takes effect");
    ASSERT_EQ_MSG((u32)2, localization_resident_table_count(loc), "the tag it superseded was never loaded");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_default_table_that_fails_to_load_leaves_no_fallback_layer) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // A manifest that parses fine but names a default whose file is absent. The de table beside it is real, so the
    // run can still put an active language on top of the missing fallback layer.
    FlArena* arena = arena_new();
    FlString dir_path
        = file_build_path_os(arena, get_current_directory_os(arena), FL_S("unit_test_localization_nodef"));
    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));

    const u8 manifest[]
        = "{\"version\":1,\"default\":\"en\","
          "\"languages\":[{\"tag\":\"en\",\"file\":\"en.json\"},{\"tag\":\"de\",\"file\":\"de.json\"}]}";
    FlString manifest_path = file_build_path_os(arena, dir_path, FL_S("manifest.json"));
    ASSERT_EQ((int)sizeof(manifest) - 1, (int)file_write_os(manifest_path, manifest, (i64)sizeof(manifest) - 1));

    const u8 de[] = "{\"version\":1,\"strings\":{\"SID_settings_title\":\"Einstellungen\"}}";
    FlString de_path = file_build_path_os(arena, dir_path, FL_S("de.json"));
    ASSERT_EQ((int)sizeof(de) - 1, (int)file_write_os(de_path, de, (i64)sizeof(de) - 1));

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(dir_path);
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "the scratch directory should mount through LocalFS");
    vfs_wait_all();

    fl_localization_init(mount.mount, FL_S("manifest.json"), false);
    struct Localization* loc = ctx->shared->localization;
    localization_settle(loc);

    ASSERT_EQ_MSG((u32)0, localization_resident_table_count(loc),
                  "a default table that failed to load is not resident");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("SID_settings_title")),
                    "with no fallback layer every SID renders as itself rather than crashing");

    fl_localization_set_language(FL_S("de"));
    localization_settle(loc);
    ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc),
                  "a language switched in afterwards is the only resident table");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Einstellungen")),
                    "the active language still works without a fallback layer beneath it");

    localization_fixture_release(ctx, mount.mount);
    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

UTEST(localization, a_default_tag_the_manifest_does_not_list_never_starts_a_table_read) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // The default names a tag absent from the language list, so no table read is ever started and localization
    // reaches its ready state without passing through a default-table load at all.
    FlArena* arena = arena_new();
    FlString dir_path
        = file_build_path_os(arena, get_current_directory_os(arena), FL_S("unit_test_localization_notag"));
    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));

    const u8 manifest[] = "{\"version\":1,\"default\":\"en\","
                          "\"languages\":[{\"tag\":\"de\",\"file\":\"de.json\"}]}";
    FlString manifest_path = file_build_path_os(arena, dir_path, FL_S("manifest.json"));
    ASSERT_EQ((int)sizeof(manifest) - 1, (int)file_write_os(manifest_path, manifest, (i64)sizeof(manifest) - 1));

    const u8 de[] = "{\"version\":1,\"strings\":{\"SID_settings_title\":\"Einstellungen\"}}";
    FlString de_path = file_build_path_os(arena, dir_path, FL_S("de.json"));
    ASSERT_EQ((int)sizeof(de) - 1, (int)file_write_os(de_path, de, (i64)sizeof(de) - 1));

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(dir_path);
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "the scratch directory should mount through LocalFS");
    vfs_wait_all();

    fl_localization_init(mount.mount, FL_S("manifest.json"), false);
    struct Localization* loc = ctx->shared->localization;
    localization_settle(loc);

    ASSERT_EQ_MSG((u32)0, localization_resident_table_count(loc), "an unlistable default resolves to no table");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("SID_settings_title")),
                    "every SID renders as itself while no table is resident");

    fl_localization_set_language(FL_S("de"));
    localization_settle(loc);
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Einstellungen")),
                    "localization is ready enough for a switch even though the default never loaded");

    localization_fixture_release(ctx, mount.mount);
    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}
