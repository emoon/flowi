// Localization hot reload tests

#include "localization/common.h"
#include <math.h>

// Hot reload: with the opt-in flag on, editing a resident table or the manifest on disk re-runs the switch
// machinery, so a reload lands at a frame boundary and a file that stops parsing leaves what is loaded in place.

static const char k_hot_manifest[] = "{ \"version\": 1, \"default\": \"en\", \"languages\": ["
                                     "{ \"tag\": \"en\", \"file\": \"en.json\" },"
                                     "{ \"tag\": \"de\", \"file\": \"de.json\" } ] }";

static const char k_hot_en[] = "{ \"version\": 1, \"strings\": {"
                               "\"SID_hot\": \"before\", \"SID_only_default\": \"fallback\" } }";

static const char k_hot_de[] = "{ \"version\": 1, \"strings\": { \"SID_hot\": \"vorher\" } }";

#define HOT_RELOAD_WAIT_ITERATIONS 5000

typedef struct LocalizationHotReload {
    FlArena* arena;
    FlString dir;
    FlVfsMount* mount;
    FlUiCtx* ctx;
} LocalizationHotReload;

static bool hot_reload_write(const LocalizationHotReload* fixture, FlString name, const char* text) {
    FlString path = file_build_path_os(fixture->arena, fixture->dir, name);
    const i64 length = (i64)strlen(text);
    return file_write_os(path, (const u8*)text, length) == length;
}

static bool hot_reload_write_initial_files(const LocalizationHotReload* fixture) {
    const struct {
        const char* name;
        const char* text;
    } files[] = { { "manifest.json", k_hot_manifest }, { "en.json", k_hot_en }, { "de.json", k_hot_de } };
    for (size_t i = 0; i < sizeof_array(files); i++) {
        if (!hot_reload_write(fixture, string_from_cstr_len(files[i].name, strlen(files[i].name)), files[i].text)) {
            return false;
        }
    }
    return true;
}

// A scratch localization directory the case can rewrite during the run, mounted with file watching on so hot
// reload sees the changes, with the default language already loaded.
static bool hot_reload_fixture_create(LocalizationHotReload* fixture, FlString dir_name, bool hot_reload) {
    fixture->arena = arena_new();
    fixture->dir = file_build_path_os(fixture->arena, get_current_directory_os(fixture->arena), dir_name);
    file_remove_directory_recursive_os(fixture->dir); // clean any leftover from an aborted run
    if (!file_create_directory_os(fixture->dir)) {
        return false;
    }

    if (!hot_reload_write_initial_files(fixture)) {
        return false;
    }

    fixture->ctx = render_fixture_get();
    if (fixture->ctx == nullptr) {
        return false;
    }

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(fixture->dir);
    if (mount.status != FlVfsMountErrorStatus_Success) {
        return false;
    }
    fixture->mount = mount.mount;
    vfs_wait_all();

    fl_localization_init(fixture->mount, FL_S("manifest.json"), hot_reload);
    localization_settle(fixture->ctx->shared->localization);
    return true;
}

static void hot_reload_fixture_release(LocalizationHotReload* fixture) {
    localization_fixture_release(fixture->ctx, fixture->mount);
    file_remove_directory_recursive_os(fixture->dir);
    fl_arena_destroy(fixture->arena);
}

// Runs the frame boundary as an app tick loop does until the watcher has asked for a reload beyond
// requests_before and the work it started has finished. False if no request arrives within the deadline.
static bool hot_reload_wait_for_request(const LocalizationHotReload* fixture, u64 requests_before) {
    struct Localization* loc = fixture->ctx->shared->localization;

    for (u32 i = 0; i < HOT_RELOAD_WAIT_ITERATIONS; i++) {
        localization_frame_begin(loc);
        if (localization_hot_reload_request_count(loc) > requests_before && !localization_is_loading(loc)) {
            return true;
        }
        fl_os_sleep_us(1000);
    }

    return false;
}

static u64 hot_reload_requests(const LocalizationHotReload* fixture) {
    return localization_hot_reload_request_count(fixture->ctx->shared->localization);
}

// Runs frame boundaries until sid resolves to expected. False if it does not within iterations milliseconds.
static bool hot_reload_wait_for(const LocalizationHotReload* fixture, FlString sid, FlString expected, u32 iterations) {
    struct Localization* loc = fixture->ctx->shared->localization;

    for (u32 i = 0; i < iterations; i++) {
        localization_frame_begin(loc);
        if (!localization_is_loading(loc) && fl_string_equals(localization_lookup(loc, sid), expected)) {
            return true;
        }
        fl_os_sleep_us(1000);
    }

    return false;
}

static FlString hot_reload_lookup(const LocalizationHotReload* fixture, FlString sid) {
    return localization_lookup(fixture->ctx->shared->localization, sid);
}

static void hot_reload_start(int* utest_result, LocalizationHotReload* fixture, FlString dir_name, bool hot_reload) {
    ASSERT_TRUE_MSG(hot_reload_fixture_create(fixture, dir_name, hot_reload), "hot reload fixture creation failed");
}

static void hot_reload_edit(int* utest_result, const LocalizationHotReload* fixture, FlString file_name,
                            const char* text) {
    ASSERT_TRUE_MSG(hot_reload_write(fixture, file_name, text), "writing the scratch file failed");
}

// Overwrites file_name with text that does not parse and waits for the watcher to report the edit.
static void hot_reload_break_file(int* utest_result, LocalizationHotReload* fixture, FlString file_name) {
    const u64 requests = hot_reload_requests(fixture);
    hot_reload_edit(utest_result, fixture, file_name, "{ this is not json");
    RETURN_IF_TEST_FAILED();
    ASSERT_TRUE_MSG(hot_reload_wait_for_request(fixture, requests), "the edit never reached the watcher");
}

// Switches the active language and waits for the switch to land.
static void hot_reload_switch_language(const LocalizationHotReload* fixture, FlString tag) {
    fl_localization_set_language(tag);
    localization_settle(fixture->ctx->shared->localization);
}

#define ASSERT_HOT_STRING(fixture, sid, expected, message) \
    ASSERT_STRING_CHECKS(STRING_CHECK(hot_reload_lookup(&(fixture), FL_S(sid)), FL_S(expected), (message)))

UTEST(localization, hot_reload_swaps_in_an_edited_table) {
    LocalizationHotReload fixture = { 0 };
    hot_reload_start(utest_result, &fixture, FL_S("unit_test_loc_hot_active_tmp"), true);
    RETURN_IF_TEST_FAILED();
    ASSERT_HOT_STRING(fixture, "SID_hot", "before", "the table starts out as loaded");

    hot_reload_edit(utest_result, &fixture, FL_S("en.json"),
                    "{ \"version\": 1, \"strings\": { \"SID_hot\": \"after\" } }");
    RETURN_IF_TEST_FAILED();
    ASSERT_TRUE_MSG(hot_reload_wait_for(&fixture, FL_S("SID_hot"), FL_S("after"), 3000),
                    "an edited table must swap its new strings in");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, hot_reload_of_the_default_table_refreshes_the_fallback_layer) {
    LocalizationHotReload fixture = { 0 };
    hot_reload_start(utest_result, &fixture, FL_S("unit_test_loc_hot_default_tmp"), true);
    RETURN_IF_TEST_FAILED();

    hot_reload_switch_language(&fixture, FL_S("de"));
    ASSERT_HOT_STRING(fixture, "SID_hot", "vorher", "the German table is active");
    ASSERT_HOT_STRING(fixture, "SID_only_default", "fallback", "the default table backs the active one");

    hot_reload_edit(utest_result, &fixture, FL_S("en.json"),
                    "{ \"version\": 1, \"strings\": { \"SID_only_default\": \"edited fallback\" } }");
    RETURN_IF_TEST_FAILED();
    ASSERT_TRUE_MSG(hot_reload_wait_for(&fixture, FL_S("SID_only_default"), FL_S("edited fallback"), 3000),
                    "the reloaded default table must become the fallback layer");
    ASSERT_HOT_STRING(fixture, "SID_hot", "vorher",
                      "reloading the default table must leave the active language where it is");
    ASSERT_EQ_MSG(2u, localization_resident_table_count(fixture.ctx->shared->localization),
                  "the reload must replace the fallback layer rather than add a third table");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, a_batch_that_edits_both_resident_tables_reloads_both) {
    LocalizationHotReload fixture = { 0 };
    hot_reload_start(utest_result, &fixture, FL_S("unit_test_loc_hot_both_tmp"), true);
    RETURN_IF_TEST_FAILED();

    hot_reload_switch_language(&fixture, FL_S("de"));
    ASSERT_HOT_STRING(fixture, "SID_hot", "vorher", "the German table is active");

    // Both resident tables edited before a single drain, so both changes arrive in one batch. Only one switch runs
    // at a time and a watcher reports a change once, so the one that cannot start immediately has to be held.
    hot_reload_edit(utest_result, &fixture, FL_S("de.json"),
                    "{ \"version\": 1, \"strings\": { \"SID_hot\": \"nachher\" } }");
    RETURN_IF_TEST_FAILED();
    hot_reload_edit(utest_result, &fixture, FL_S("en.json"),
                    "{ \"version\": 1, \"strings\": { \"SID_only_default\": \"edited fallback\" } }");
    RETURN_IF_TEST_FAILED();

    ASSERT_TRUE_MSG(hot_reload_wait_for(&fixture, FL_S("SID_only_default"), FL_S("edited fallback"), 3000),
                    "the second table's reload must be held, not dropped, while the first one runs");
    ASSERT_HOT_STRING(fixture, "SID_hot", "nachher", "the active table's own reload must land as well");
    ASSERT_EQ_MSG(2u, localization_resident_table_count(fixture.ctx->shared->localization),
                  "reloading both tables must replace them rather than add more");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, a_hot_reload_that_fails_to_parse_keeps_the_current_table) {
    LocalizationHotReload fixture = { 0 };
    hot_reload_start(utest_result, &fixture, FL_S("unit_test_loc_hot_broken_tmp"), true);
    RETURN_IF_TEST_FAILED();

    hot_reload_break_file(utest_result, &fixture, FL_S("en.json"));
    RETURN_IF_TEST_FAILED();

    ASSERT_HOT_STRING(fixture, "SID_hot", "before", "a table that stops parsing must leave the loaded one in place");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, a_default_table_reload_that_fails_to_parse_keeps_the_fallback_layer) {
    LocalizationHotReload fixture = { 0 };
    hot_reload_start(utest_result, &fixture, FL_S("unit_test_loc_hot_broken_default_tmp"), true);
    RETURN_IF_TEST_FAILED();

    hot_reload_switch_language(&fixture, FL_S("de"));
    ASSERT_HOT_STRING(fixture, "SID_only_default", "fallback", "the default table backs the active one");

    hot_reload_break_file(utest_result, &fixture, FL_S("en.json"));
    RETURN_IF_TEST_FAILED();

    ASSERT_STRING_CHECKS(STRING_CHECK(hot_reload_lookup(&fixture, FL_S("SID_only_default")), FL_S("fallback"),
                                      "a default table that stops parsing must leave the fallback layer in place"),
                         STRING_CHECK(hot_reload_lookup(&fixture, FL_S("SID_hot")), FL_S("vorher"),
                                      "a failed default-table reload must leave the active language alone"));
    ASSERT_EQ_MSG(2u, localization_resident_table_count(fixture.ctx->shared->localization),
                  "a failed reload must not leave a third table resident");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, hot_reload_picks_up_a_manifest_edit) {
    LocalizationHotReload fixture = { 0 };
    hot_reload_start(utest_result, &fixture, FL_S("unit_test_loc_hot_manifest_tmp"), true);
    RETURN_IF_TEST_FAILED();

    const u64 requests = hot_reload_requests(&fixture);
    hot_reload_edit(utest_result, &fixture, FL_S("de2.json"),
                    "{ \"version\": 1, \"strings\": { \"SID_hot\": \"zwei\" } }");
    RETURN_IF_TEST_FAILED();
    hot_reload_edit(utest_result, &fixture, FL_S("manifest.json"),
                    "{ \"version\": 1, \"default\": \"en\", \"languages\": ["
                    "{ \"tag\": \"en\", \"file\": \"en.json\" },"
                    "{ \"tag\": \"de\", \"file\": \"de2.json\" } ] }");
    RETURN_IF_TEST_FAILED();
    ASSERT_TRUE_MSG(hot_reload_wait_for_request(&fixture, requests), "the manifest edit never reached the watcher");

    hot_reload_switch_language(&fixture, FL_S("de"));
    ASSERT_HOT_STRING(fixture, "SID_hot", "zwei",
                      "a switch after a manifest edit must use the file the new language list names");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, an_unparseable_manifest_reload_keeps_the_last_good_language_list) {
    LocalizationHotReload fixture = { 0 };
    hot_reload_start(utest_result, &fixture, FL_S("unit_test_loc_hot_bad_manifest_tmp"), true);
    RETURN_IF_TEST_FAILED();

    hot_reload_break_file(utest_result, &fixture, FL_S("manifest.json"));
    RETURN_IF_TEST_FAILED();

    hot_reload_switch_language(&fixture, FL_S("de"));
    ASSERT_HOT_STRING(fixture, "SID_hot", "vorher",
                      "a manifest that stops parsing must leave the last good language list in force");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, hot_reload_off_never_reloads) {
    LocalizationHotReload fixture = { 0 };
    hot_reload_start(utest_result, &fixture, FL_S("unit_test_loc_hot_off_tmp"), false);
    RETURN_IF_TEST_FAILED();

    ASSERT_FALSE_MSG(localization_is_watching(fixture.ctx->shared->localization),
                     "a run with hot reload off registers no watch");
    hot_reload_edit(utest_result, &fixture, FL_S("en.json"),
                    "{ \"version\": 1, \"strings\": { \"SID_hot\": \"after\" } }");
    RETURN_IF_TEST_FAILED();
    localization_frame_begin(fixture.ctx->shared->localization);
    ASSERT_HOT_STRING(fixture, "SID_hot", "before", "a run with hot reload off keeps the table it loaded");

    hot_reload_fixture_release(&fixture);
}

// Waits until the watcher has been told of a change. False if none arrives within the deadline.
static bool hot_reload_wait_for_independent_watcher(FlFileWatcherHandle watcher) {
    for (u32 i = 0; i < HOT_RELOAD_WAIT_ITERATIONS; i++) {
        if (file_watcher_has_changes(watcher)) {
            return true;
        }
        fl_os_sleep_us(1000);
    }
    return false;
}

UTEST(localization, a_file_change_does_not_drop_a_language_switch_that_has_not_been_applied) {
    LocalizationHotReload fixture = { 0 };
    hot_reload_start(utest_result, &fixture, FL_S("unit_test_loc_hot_race_tmp"), true);
    RETURN_IF_TEST_FAILED();

    // Queue the edit and wait until the kernel has delivered it to an independent watcher before recording the
    // switch, so the frame boundary drains a watcher that already has the change while the switch is still only
    // recorded.
    const FlFileWatcherConfig barrier_config
        = { .recursive = true, .watch_files = true, .watch_directories = false, .max_changes = 0 };
    const FlFileWatcherHandle barrier = file_watcher_start(fixture.dir, barrier_config);
    ASSERT_NE(barrier, FL_FILE_WATCHER_INVALID);
    const bool written
        = hot_reload_write(&fixture, FL_S("en.json"), "{ \"version\": 1, \"strings\": { \"SID_hot\": \"after\" } }");
    const bool delivered = hot_reload_wait_for_independent_watcher(barrier);
    file_watcher_stop(barrier);
    ASSERT_TRUE(written);
    ASSERT_TRUE_MSG(delivered, "the edit never reached the independent watcher");

    hot_reload_switch_language(&fixture, FL_S("de"));
    ASSERT_HOT_STRING(fixture, "SID_hot", "vorher",
                      "a file change must not overwrite a language switch that is still pending");

    hot_reload_fixture_release(&fixture);
}
