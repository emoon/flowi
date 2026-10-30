#include "utest/utest.h"
#include <core/arena.h>
#include <core/string.h>
#include <tools/loc_check.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fixtures for the drift checker. Every case builds its tables as in-memory JSON5 and asserts on the findings the
// six checks produce, so nothing here touches the filesystem.

static u32 count_findings(const LocCheckResult* result, LocCheckId check) {
    u32 count = 0;
    for (const LocCheckFinding* f = result->first; f != nullptr; f = f->next) {
        if (f->check == check) {
            count++;
        }
    }
    return count;
}

static const LocCheckFinding* first_finding(const LocCheckResult* result, LocCheckId check) {
    for (const LocCheckFinding* f = result->first; f != nullptr; f = f->next) {
        if (f->check == check) {
            return f;
        }
    }
    return nullptr;
}

#define ASSERT_NO_FINDINGS(result)       \
    ASSERT_EQ((result).error_count, 0u); \
    ASSERT_EQ((result).warning_count, 0u)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, matching_code_and_tables_are_clean) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckSource sources[] = { { .path = FL_S("app.c"), .text = FL_S("ui_text(FL_S(\"SID_title\"));") } };
    const LocCheckTable tables[] = {
        { .tag = FL_S("en"), .path = FL_S("en.json"), .json = FL_S("{\"strings\":{\"SID_title\":\"Title\"}}") },
        { .tag = FL_S("de"), .path = FL_S("de.json"), .json = FL_S("{\"strings\":{\"SID_title\":\"Titel\"}}") },
    };

    const LocCheckResult result = loc_check_run(arena, sources, 1, tables, 2);
    ASSERT_NO_FINDINGS(result);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, key_used_in_code_but_absent_from_the_default_is_an_error) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckSource sources[]
        = { { .path = FL_S("app.c"), .text = FL_S("ui_text(FL_S(\"SID_title\"));\nui_text(FL_S(\"SID_missing\"));") } };
    const LocCheckTable tables[]
        = { { .tag = FL_S("en"), .path = FL_S("en.json"), .json = FL_S("{\"strings\":{\"SID_title\":\"Title\"}}") } };

    const LocCheckResult result = loc_check_run(arena, sources, 1, tables, 1);
    ASSERT_EQ(count_findings(&result, LocCheckId_MissingFromDefault), 1u);
    ASSERT_EQ(result.error_count, 1u);

    // Code-side findings carry file:line so the author can jump straight to the call.
    const LocCheckFinding* finding = first_finding(&result, LocCheckId_MissingFromDefault);
    ASSERT_TRUE(finding->is_error);
    ASSERT_TRUE(fl_string_equals(finding->message, FL_S("app.c:2: SID_missing")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, key_never_seen_in_code_is_an_orphan_warning) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckSource sources[] = { { .path = FL_S("app.c"), .text = FL_S("ui_text(FL_S(\"SID_title\"));") } };
    const LocCheckTable tables[]
        = { { .tag = FL_S("en"),
              .path = FL_S("en.json"),
              .json = FL_S("{\"strings\":{\"SID_title\":\"Title\",\"SID_stale\":\"Stale\"}}") } };

    const LocCheckResult result = loc_check_run(arena, sources, 1, tables, 1);
    ASSERT_EQ(count_findings(&result, LocCheckId_Orphan), 1u);
    ASSERT_EQ(result.error_count, 0u);
    ASSERT_TRUE(fl_string_equals(first_finding(&result, LocCheckId_Orphan)->message, FL_S("SID_stale")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, without_sources_the_code_side_checks_do_not_run) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[]
        = { { .tag = FL_S("en"), .path = FL_S("en.json"), .json = FL_S("{\"strings\":{\"SID_title\":\"Title\"}}") } };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 1);
    ASSERT_NO_FINDINGS(result);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, a_dynamic_sid_looks_orphaned_rather_than_missing) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckSource sources[]
        = { { .path = FL_S("app.c"),
              .text = FL_S("sprintf(key, \"SID_tab_%d\", index);\nui_text(FL_S(\"SID_title\"));") } };
    const LocCheckTable tables[]
        = { { .tag = FL_S("en"),
              .path = FL_S("en.json"),
              .json = FL_S("{\"strings\":{\"SID_title\":\"Title\",\"SID_tab_0\":\"First\"}}") } };

    const LocCheckResult result = loc_check_run(arena, sources, 1, tables, 1);
    // The literal prefix "SID_tab_%d" is scanned as a key of its own: unresolvable, hence the error, while the key
    // it assembles at runtime is invisible and its entry reads as an orphan. Both are the documented tolerance.
    ASSERT_EQ(count_findings(&result, LocCheckId_MissingFromDefault), 1u);
    ASSERT_EQ(count_findings(&result, LocCheckId_Orphan), 1u);
    ASSERT_TRUE(fl_string_equals(first_finding(&result, LocCheckId_Orphan)->message, FL_S("SID_tab_0")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, keys_missing_from_a_translation_are_that_language_s_worklist) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[] = {
        { .tag = FL_S("en"),
          .path = FL_S("en.json"),
          .json = FL_S("{\"strings\":{\"SID_title\":\"Title\",\"SID_body\":\"Body\"}}") },
        { .tag = FL_S("de"), .path = FL_S("de.json"), .json = FL_S("{\"strings\":{\"SID_title\":\"Titel\"}}") },
    };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 2);
    ASSERT_EQ(count_findings(&result, LocCheckId_MissingFromTranslation), 1u);
    ASSERT_EQ(result.error_count, 0u);

    const LocCheckFinding* finding = first_finding(&result, LocCheckId_MissingFromTranslation);
    ASSERT_EQ(finding->language, 1u);
    ASSERT_TRUE(fl_string_equals(finding->message, FL_S("SID_body")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, a_translation_key_the_default_dropped_is_stale) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[] = {
        { .tag = FL_S("en"), .path = FL_S("en.json"), .json = FL_S("{\"strings\":{\"SID_title\":\"Title\"}}") },
        { .tag = FL_S("de"),
          .path = FL_S("de.json"),
          .json = FL_S("{\"strings\":{\"SID_title\":\"Titel\",\"SID_gone\":\"Weg\"}}") },
    };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 2);
    ASSERT_EQ(count_findings(&result, LocCheckId_Stale), 1u);
    ASSERT_EQ(result.error_count, 0u);
    ASSERT_TRUE(fl_string_equals(first_finding(&result, LocCheckId_Stale)->message, FL_S("SID_gone")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, repeated_reordered_and_unused_slots_are_legal) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[] = {
        { .tag = FL_S("en"),
          .path = FL_S("en.json"),
          .json = FL_S("{\"strings\":{\"SID_a\":\"{0} of {1}\",\"SID_b\":\"{{0}} and {0}\",\"SID_c\":\"{0} {1}\"}}") },
        // Repeated: {0} three times. Reordered: {1} before {0}. Unused: {1} dropped entirely.
        { .tag = FL_S("de"),
          .path = FL_S("de.json"),
          .json = FL_S("{\"strings\":{\"SID_a\":\"{1} von {0}, {0}, {0}\",\"SID_b\":\"{0}\",\"SID_c\":\"{0}\"}}") },
    };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 2);
    ASSERT_NO_FINDINGS(result);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, a_slot_above_the_default_s_highest_is_an_error) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[] = {
        { .tag = FL_S("en"), .path = FL_S("en.json"), .json = FL_S("{\"strings\":{\"SID_a\":\"{0} of {1}\"}}") },
        { .tag = FL_S("de"),
          .path = FL_S("de.json"),
          .json = FL_S("{\"strings\":{\"SID_a\":\"{0} von {1} in {2}\"}}") },
    };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 2);
    ASSERT_EQ(count_findings(&result, LocCheckId_PlaceholderArity), 1u);
    ASSERT_EQ(result.error_count, 1u);

    const LocCheckFinding* finding = first_finding(&result, LocCheckId_PlaceholderArity);
    ASSERT_TRUE(finding->is_error);
    ASSERT_TRUE(fl_string_equals(finding->message, FL_S("SID_a references {2}, but the default table goes no higher "
                                                        "than {1}")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, an_escaped_brace_in_the_default_leaves_no_slot_to_reference) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[] = {
        { .tag = FL_S("en"),
          .path = FL_S("en.json"),
          .json = FL_S("{\"strings\":{\"SID_a\":\"{{0}} shows nothing\"}}") },
        { .tag = FL_S("de"), .path = FL_S("de.json"), .json = FL_S("{\"strings\":{\"SID_a\":\"{0}\"}}") },
    };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 2);
    ASSERT_EQ(count_findings(&result, LocCheckId_PlaceholderArity), 1u);
    ASSERT_TRUE(fl_string_equals(first_finding(&result, LocCheckId_PlaceholderArity)->message,
                                 FL_S("SID_a references {0}, but the default table uses no placeholders")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, a_named_slot_is_not_an_argument_index) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[] = {
        { .tag = FL_S("en"), .path = FL_S("en.json"), .json = FL_S("{\"strings\":{\"SID_a\":\"{0} files\"}}") },
        { .tag = FL_S("de"), .path = FL_S("de.json"), .json = FL_S("{\"strings\":{\"SID_a\":\"{count} Dateien\"}}") },
    };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 2);
    ASSERT_NO_FINDINGS(result);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, plural_variants_are_compared_at_their_widest) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[] = {
        { .tag = FL_S("en"),
          .path = FL_S("en.json"),
          .json = FL_S("{\"strings\":{\"SID_n\":{\"one\":\"{0} file\",\"other\":\"{0} files\"}}}") },
        { .tag = FL_S("de"),
          .path = FL_S("de.json"),
          .json = FL_S("{\"strings\":{\"SID_n\":{\"one\":\"{0} Datei\",\"other\":\"{0} von {1}\"}}}") },
    };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 2);
    ASSERT_EQ(count_findings(&result, LocCheckId_PlaceholderArity), 1u);
    ASSERT_EQ(result.error_count, 1u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, a_well_formed_plural_object_passes) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[] = { { .tag = FL_S("en"),
                                       .path = FL_S("en.json"),
                                       .json = FL_S("{\"strings\":{\"SID_n\":{\"zero\":\"none\",\"one\":\"one\","
                                                    "\"two\":\"two\",\"few\":\"few\",\"many\":\"many\","
                                                    "\"other\":\"lots\"}}}") } };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 1);
    ASSERT_NO_FINDINGS(result);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, a_non_cldr_variant_errors_in_the_default_and_warns_in_a_translation) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[] = {
        { .tag = FL_S("en"),
          .path = FL_S("en.json"),
          .json = FL_S("{\"strings\":{\"SID_n\":{\"singular\":\"one\",\"other\":\"lots\"}}}") },
        { .tag = FL_S("de"),
          .path = FL_S("de.json"),
          .json = FL_S("{\"strings\":{\"SID_n\":{\"einzahl\":\"eins\",\"other\":\"viele\"}}}") },
    };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 2);
    ASSERT_EQ(count_findings(&result, LocCheckId_ValueShape), 2u);
    ASSERT_EQ(result.error_count, 1u);
    ASSERT_EQ(result.warning_count, 1u);

    const LocCheckFinding* finding = first_finding(&result, LocCheckId_ValueShape);
    ASSERT_EQ(finding->language, 0u);
    ASSERT_TRUE(finding->is_error);
    ASSERT_TRUE(fl_string_equals(finding->message, FL_S("SID_n has the non-CLDR variant \"singular\"")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, a_plural_object_without_other_is_reported) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[] = {
        { .tag = FL_S("en"), .path = FL_S("en.json"), .json = FL_S("{\"strings\":{\"SID_n\":{\"one\":\"one\"}}}") }
    };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 1);
    ASSERT_EQ(count_findings(&result, LocCheckId_ValueShape), 1u);
    ASSERT_EQ(result.error_count, 1u);
    ASSERT_TRUE(fl_string_equals(first_finding(&result, LocCheckId_ValueShape)->message,
                                 FL_S("SID_n has no \"other\" variant")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, a_value_that_is_neither_string_nor_object_is_reported) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[]
        = { { .tag = FL_S("en"), .path = FL_S("en.json"), .json = FL_S("{\"strings\":{\"SID_n\":42}}") } };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 1);
    ASSERT_EQ(count_findings(&result, LocCheckId_ValueShape), 1u);
    ASSERT_EQ(result.error_count, 1u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, an_unparseable_table_is_an_error_and_takes_no_other_check_down) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[] = {
        { .tag = FL_S("en"), .path = FL_S("en.json"), .json = FL_S("{\"strings\":{\"SID_title\":\"Title\"}}") },
        { .tag = FL_S("de"), .path = FL_S("de.json"), .json = FL_S("{ not json") },
        { .tag = FL_S("fr"), .path = FL_S("fr.json") },
    };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 3);
    ASSERT_EQ(count_findings(&result, LocCheckId_TableUnreadable), 2u);
    ASSERT_EQ(count_findings(&result, LocCheckId_MissingFromTranslation), 0u);
    ASSERT_EQ(result.error_count, 2u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, the_tables_are_read_with_json5_comments_and_trailing_commas) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[] = { { .tag = FL_S("en"),
                                       .path = FL_S("en.json"),
                                       .json = FL_S("{\n  // translator context lives here\n"
                                                    "  \"strings\": {\n    \"SID_title\": \"Title\",\n  },\n}") } };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 1);
    ASSERT_NO_FINDINGS(result);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, the_report_groups_per_language_and_ends_in_a_summary) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckSource sources[] = { { .path = FL_S("app.c"), .text = FL_S("ui_text(FL_S(\"SID_missing\"));") } };
    const LocCheckTable tables[] = {
        { .tag = FL_S("en"), .path = FL_S("en.json"), .json = FL_S("{\"strings\":{\"SID_title\":\"Title\"}}") },
        { .tag = FL_S("de"), .path = FL_S("de.json"), .json = FL_S("{\"strings\":{}}") },
    };

    const LocCheckResult result = loc_check_run(arena, sources, 1, tables, 2);
    const FlString report = loc_check_format_report(arena, &result, tables, 2);

    ASSERT_TRUE(fl_string_equals(report, FL_S("en (default)\n"
                                              "  missing from the default table (1)\n"
                                              "    error   app.c:1: SID_missing\n"
                                              "  orphaned (1)\n"
                                              "    warning SID_title\n"
                                              "de\n"
                                              "  missing in de (1)\n"
                                              "    warning SID_title\n"
                                              "\n"
                                              "1 error, 2 warnings\n")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(loc_check, a_clean_run_reports_only_the_summary) {
    FlArena* arena = arena_new(.reserved_size = MB(16));

    const LocCheckTable tables[]
        = { { .tag = FL_S("en"), .path = FL_S("en.json"), .json = FL_S("{\"strings\":{\"SID_title\":\"Title\"}}") } };

    const LocCheckResult result = loc_check_run(arena, nullptr, 0, tables, 1);
    ASSERT_TRUE(fl_string_equals(loc_check_format_report(arena, &result, tables, 1), FL_S("0 errors, 0 warnings\n")));

    fl_arena_destroy(arena);
}
