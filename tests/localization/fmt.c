// Localization formatting and plural tests

#include "localization/common.h"
#include <math.h>

// fl_localize_fmt: lookup plus indexed-placeholder substitution.

static FlFmtArg fmt_int(i64 value) {
    return (FlFmtArg) { .value.int_value = value, .kind = FlFmtArgKind_Int };
}

static FlFmtArg fmt_float(f64 value) {
    return (FlFmtArg) { .value.float_value = value, .kind = FlFmtArgKind_Float };
}

static FlFmtArg fmt_str(FlString value) {
    return (FlFmtArg) { .value.string_value = value, .kind = FlFmtArgKind_Str };
}

UTEST(localization, fmt_substitutes_indexed_placeholders) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const FlFmtArg one[] = { fmt_str(FL_S("Ada")) };
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_greeting"), one, 1), FL_S("Hello, Ada!")),
                    "a single in-order slot takes the first argument");

    const FlFmtArg two[] = { fmt_str(FL_S("first")), fmt_str(FL_S("second")) };
    ASSERT_STRING_CHECKS(STRING_CHECK(fl_localize_fmt(FL_S("SID_fmt_reordered"), two, 2), FL_S("second then first"),
                                      "slots address arguments by index, in whatever order the translation puts them"),
                         STRING_CHECK(fl_localize_fmt(FL_S("SID_fmt_repeated"), two, 2), FL_S("first, first and first"),
                                      "a repeated index substitutes at every occurrence"),
                         // An argument that no slot names is unused. It is not an error.
                         STRING_CHECK(fl_localize_fmt(FL_S("SID_greeting"), two, 2), FL_S("Hello, first!"),
                                      "arguments past the slots the text uses are ignored"));

    localization_fixture_release(ctx, mount);
}

UTEST(localization, fmt_renders_doubled_braces_as_literals) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const FlFmtArg args[] = { fmt_str(FL_S("the value")) };
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_fmt_escaped"), args, 1), FL_S("{0} shows the value")),
                    "{{ and }} escape to one brace each and never open a slot");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, fmt_leaves_a_placeholder_with_no_argument_visible) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const FlFmtArg args[] = { fmt_str(FL_S("two")) };
    ASSERT_STRING_CHECKS(STRING_CHECK(fl_localize_fmt(FL_S("SID_fmt_two_slots"), args, 1), FL_S("have two of {1}"),
                                      "a slot past the end of the argument list renders as itself"),
                         STRING_CHECK(fl_localize_fmt(FL_S("SID_greeting"), nullptr, 0), FL_S("Hello, {0}!"),
                                      "a parameterized key called with no arguments keeps every placeholder visible"));

    localization_fixture_release(ctx, mount);
}

UTEST(localization, fmt_formats_numbers_c_style) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const FlFmtArg args[] = { fmt_int(-1000000), fmt_float(2.5) };
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_fmt_two_slots"), args, 2), FL_S("have -1000000 of 2.5")),
                    "integers and floats format plainly: a . decimal point and no digit grouping");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, fmt_returns_unparameterized_text_without_copying_it) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const FlString first = fl_localize_fmt(FL_S("SID_settings_title"), nullptr, 0);
    const FlString second = fl_localize_fmt(FL_S("SID_settings_title"), nullptr, 0);
    ASSERT_TRUE_MSG(fl_string_equals(first, FL_S("Settings")), "text with no placeholders resolves like any other key");
    ASSERT_TRUE_MSG(first.data == second.data,
                    "and is returned as the table's own bytes, so no scan or frame-arena copy happened");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_parameterized_key_on_the_implicit_path_shows_its_placeholders) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    char snapshot[8192];
    const int32_t needed = localization_emit(ctx, FL_S("loc_probe"), FL_S("SID_greeting"), SNAPSHOT_BUFFER(snapshot));
    ASSERT_TRUE_MSG(needed > 0 && needed < (int32_t)sizeof(snapshot), "snapshot fits the buffer");
    ASSERT_TRUE_MSG(strstr(snapshot, "Hello, {0}!") != nullptr,
                    "a widget reaching a parameterized key without arguments renders the placeholders");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, fmt_leaves_text_it_cannot_substitute_as_written) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_fmt_stray_braces"), nullptr, 0), FL_S("a } b {c")),
                    "a closing brace that opened nothing, and a slot nothing closes, are both literal text");

    const FlFmtArg unknown[] = { { .value.int_value = 7, .kind = (FlFmtArgKind)99 } };
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_greeting"), unknown, 1), FL_S("Hello, {0}!")),
                    "an argument whose kind names no arm of the union counts as no argument at all");

    localization_fixture_release(ctx, mount);
}

// fl_localize_plural: built-in variant selection, then the same substitution with the count as {0}.

typedef struct PluralCase {
    FlString language;
    i64 count;
    LocalizationPluralCategory category;
} PluralCase;

UTEST(localization, plural_rules_select_per_language_family) {
    const PluralCase cases[] = {
        // Germanic and Romance: 1 alone is "one".
        { FL_S("en"), 1, LocalizationPlural_One },
        { FL_S("en"), 0, LocalizationPlural_Other },
        { FL_S("de"), 2, LocalizationPlural_Other },
        { FL_S("sv"), 21, LocalizationPlural_Other },
        // French-style: 0 joins 1 in "one".
        { FL_S("fr"), 0, LocalizationPlural_One },
        { FL_S("fr"), 1, LocalizationPlural_One },
        { FL_S("fr"), 2, LocalizationPlural_Other },
        // Only the primary subtag decides, so a region carries its language's rules.
        { FL_S("pt-br"), 0, LocalizationPlural_One },
        { FL_S("PT-BR"), 0, LocalizationPlural_One },
        // Slavic: the last digit picks the category and the 11-14 band overrides it.
        { FL_S("ru"), 1, LocalizationPlural_One },
        { FL_S("ru"), 21, LocalizationPlural_One },
        { FL_S("ru"), 101, LocalizationPlural_One },
        { FL_S("ru"), 2, LocalizationPlural_Few },
        { FL_S("ru"), 4, LocalizationPlural_Few },
        { FL_S("uk"), 22, LocalizationPlural_Few },
        { FL_S("ru"), 0, LocalizationPlural_Many },
        { FL_S("ru"), 5, LocalizationPlural_Many },
        { FL_S("ru"), 11, LocalizationPlural_Many },
        { FL_S("ru"), 12, LocalizationPlural_Many },
        { FL_S("ru"), 14, LocalizationPlural_Many },
        { FL_S("ru"), 111, LocalizationPlural_Many },
        // CJK: every count is "other".
        { FL_S("ja"), 0, LocalizationPlural_Other },
        { FL_S("zh"), 1, LocalizationPlural_Other },
        { FL_S("ko"), 2, LocalizationPlural_Other },
        // A language with no built-in rules also selects "other", and does not borrow a neighbour's rules.
        { FL_S("ar"), 1, LocalizationPlural_Other },
        { FL_S("pl"), 2, LocalizationPlural_Other },
        { string_empty(), 1, LocalizationPlural_Other },
        // A negative count selects as its magnitude does.
        { FL_S("en"), -1, LocalizationPlural_One },
        { FL_S("ru"), -22, LocalizationPlural_Few },
        { FL_S("en"), INT64_MIN, LocalizationPlural_Other },
    };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        ASSERT_EQ((int)cases[i].category, (int)localization_plural_category(cases[i].language, cases[i].count));
    }
}

UTEST(localization, plural_selects_a_variant_and_exposes_the_count_as_the_first_argument) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    ASSERT_STRING_CHECKS(STRING_CHECK(fl_localize_plural(FL_S("SID_file_count"), 1, nullptr, 0), FL_S("1 file"),
                                      "a count of one selects the one variant and substitutes itself into {0}"),
                         STRING_CHECK(fl_localize_plural(FL_S("SID_file_count"), 3, nullptr, 0), FL_S("3 files"),
                                      "any other count selects the other variant"),
                         STRING_CHECK(fl_localize_plural(FL_S("SID_file_count"), 0, nullptr, 0), FL_S("0 files"),
                                      "English puts zero in the other variant"));

    // The caller's own arguments start at {1}, so the count cannot be passed twice by mistake.
    const FlFmtArg args[] = { fmt_str(FL_S("Documents")) };
    ASSERT_STRING_CHECKS(STRING_CHECK(fl_localize_plural(FL_S("SID_plural_args"), 1, args, 1),
                                      FL_S("1 file in Documents"), "the first caller argument fills {1}"),
                         STRING_CHECK(fl_localize_plural(FL_S("SID_plural_args"), 7, args, 1),
                                      FL_S("7 files in Documents"), "the shift holds across variants"));

    localization_fixture_release(ctx, mount);
}

UTEST(localization, plural_degrades_to_other_and_then_through_the_fallback_chain) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    // The key carries no "one" variant, so the count that selects it lands on "other".
    ASSERT_STRING_CHECKS(STRING_CHECK(fl_localize_plural(FL_S("SID_plural_other_only"), 1, nullptr, 0), FL_S("1 items"),
                                      "a variant the key does not carry degrades to other"),
                         // A plain string is its own answer whatever the count says.
                         STRING_CHECK(fl_localize_plural(FL_S("SID_settings_title"), 2, nullptr, 0), FL_S("Settings"),
                                      "a key that is a plain string resolves to it"),
                         STRING_CHECK(fl_localize_plural(FL_S("SID_absent"), 2, nullptr, 0), FL_S("SID_absent"),
                                      "a key no table carries renders as the key itself"));

    // pt-br carries the key, so the switch also puts Portuguese's rules behind it: zero counts as one there.
    fl_localization_set_language(FL_S("pt-br"));
    localization_settle(ctx->shared->localization);
    ASSERT_STRING_CHECKS(STRING_CHECK(fl_localize_plural(FL_S("SID_file_count"), 0, nullptr, 0), FL_S("0 arquivo"),
                                      "the active language's rules put zero in the one variant"),
                         STRING_CHECK(fl_localize_plural(FL_S("SID_file_count"), 2, nullptr, 0), FL_S("2 arquivos"),
                                      "and everything above one in the other variant"),
                         // A key the active language lacks falls back to the default table, rules and all.
                         STRING_CHECK(fl_localize_plural(FL_S("SID_plural_args"), 1, nullptr, 0), FL_S("1 file in {1}"),
                                      "a missing key falls back to the default language"));

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_plural_key_through_the_plain_fmt_call_renders_the_other_variant) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const FlFmtArg args[] = { fmt_int(1) };
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_file_count"), args, 1), FL_S("1 files")),
                    "the count-less call resolves the other variant rather than guessing at one");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, malformed_plural_entries_are_skipped_per_key) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlArena* arena = arena_new();
    FlString dir_path
        = file_build_path_os(arena, get_current_directory_os(arena), FL_S("unit_test_localization_plural"));
    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));

    const u8 manifest[] = "{\"version\":1,\"default\":\"en\",\"languages\":[{\"tag\":\"en\",\"file\":\"en.json\"}]}";
    FlString manifest_path = file_build_path_os(arena, dir_path, FL_S("manifest.json"));
    ASSERT_EQ((int)sizeof(manifest) - 1, (int)file_write_os(manifest_path, manifest, (i64)sizeof(manifest) - 1));

    const u8 en[] = "{\"version\":1,\"strings\":{"
                    "\"SID_ok\":{\"one\":\"{0} thing\",\"other\":\"{0} things\"},"
                    "\"SID_no_other\":{\"one\":\"{0} thing\"},"
                    "\"SID_unknown_variant\":{\"lots\":\"{0} lots\",\"other\":\"{0} others\"},"
                    "\"SID_non_string_variant\":{\"one\":5,\"other\":\"{0} others\"},"
                    "\"SID_not_a_value\":5}}";
    FlString en_path = file_build_path_os(arena, dir_path, FL_S("en.json"));
    ASSERT_EQ((int)sizeof(en) - 1, (int)file_write_os(en_path, en, (i64)sizeof(en) - 1));

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(dir_path);
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "the scratch directory should mount through LocalFS");
    vfs_wait_all();

    fl_localization_init(mount.mount, FL_S("manifest.json"), false);
    localization_settle(ctx->shared->localization);

    ASSERT_STRING_CHECKS(
        STRING_CHECK(fl_localize_plural(FL_S("SID_ok"), 1, nullptr, 0), FL_S("1 thing"),
                     "a well-formed variant object resolves"),
        STRING_CHECK(fl_localize_plural(FL_S("SID_no_other"), 1, nullptr, 0), FL_S("SID_no_other"),
                     "a variant object with no other variant is skipped and falls through"),
        STRING_CHECK(fl_localize_plural(FL_S("SID_unknown_variant"), 2, nullptr, 0), FL_S("2 others"),
                     "an unknown variant name is ignored and the rest of the key survives"),
        STRING_CHECK(fl_localize_plural(FL_S("SID_non_string_variant"), 1, nullptr, 0), FL_S("1 others"),
                     "a non-string variant is ignored and its category degrades to other"),
        STRING_CHECK(fl_localize_plural(FL_S("SID_not_a_value"), 1, nullptr, 0), FL_S("SID_not_a_value"),
                     "a value that is neither a string nor an object is skipped"));

    localization_fixture_release(ctx, mount.mount);
    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

UTEST(localization, a_language_with_no_built_in_rules_selects_the_other_variant) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlArena* arena = arena_new();
    FlString dir_path
        = file_build_path_os(arena, get_current_directory_os(arena), FL_S("unit_test_localization_norule"));
    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));

    const u8 manifest[] = "{\"version\":1,\"default\":\"xx\",\"languages\":[{\"tag\":\"xx\",\"file\":\"xx.json\"}]}";
    FlString manifest_path = file_build_path_os(arena, dir_path, FL_S("manifest.json"));
    ASSERT_EQ((int)sizeof(manifest) - 1, (int)file_write_os(manifest_path, manifest, (i64)sizeof(manifest) - 1));

    const u8 xx[] = "{\"version\":1,\"strings\":{\"SID_count\":{\"one\":\"{0} thing\",\"other\":\"{0} things\"}}}";
    FlString xx_path = file_build_path_os(arena, dir_path, FL_S("xx.json"));
    ASSERT_EQ((int)sizeof(xx) - 1, (int)file_write_os(xx_path, xx, (i64)sizeof(xx) - 1));

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(dir_path);
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "the scratch directory should mount through LocalFS");
    vfs_wait_all();

    fl_localization_init(mount.mount, FL_S("manifest.json"), false);
    localization_settle(ctx->shared->localization);

    // The count that would select "one" in a language flowi has rules for selects "other" here, and the warning
    // is logged once and not once per lookup.
    ASSERT_STRING_CHECKS(STRING_CHECK(fl_localize_plural(FL_S("SID_count"), 1, nullptr, 0), FL_S("1 things"),
                                      "a language with no built-in rules selects the other variant for every count"),
                         STRING_CHECK(fl_localize_plural(FL_S("SID_count"), 1, nullptr, 0), FL_S("1 things"),
                                      "and keeps doing so once it has warned"));

    localization_fixture_release(ctx, mount.mount);
    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

// FL_FMT / FL_PLURAL: the C call-site macros that tag bare arguments and build the FlFmtArg array at the call.

#include <flowi/ui/localization_fmt.h>

UTEST(localization, fmt_macro_tags_bare_arguments_by_type) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    ASSERT_STRING_CHECKS(
        STRING_CHECK(FL_FMT("SID_greeting", "Ada"), FL_S("Hello, Ada!"), "a C string argument tags as Str"),
        STRING_CHECK(FL_FMT("SID_greeting", FL_S("Grace")), FL_S("Hello, Grace!"), "an FlString argument tags as Str"));

    const i32 files = -1000000;
    const f32 ratio = 2.5f;
    ASSERT_STRING_CHECKS(STRING_CHECK(FL_FMT("SID_fmt_two_slots", files, ratio), FL_S("have -1000000 of 2.5"),
                                      "integer and float arguments tag as Int and Float and format C-style"),
                         STRING_CHECK(FL_FMT("SID_fmt_two_slots", FL_ARG_INT(7), FL_ARG_FLOAT(0.5)),
                                      FL_S("have 7 of 0.5"),
                                      "the explicit constructors sit in the same argument list as bare values"),
                         STRING_CHECK(FL_FMT("SID_greeting", FL_ARG_STR(FL_S("Ada"))), FL_S("Hello, Ada!"),
                                      "an explicitly tagged string reaches the same slot a bare one would"),
                         STRING_CHECK(FL_FMT("SID_settings_title"), FL_S("Settings"),
                                      "a key with no placeholders takes no arguments at all"));

    localization_fixture_release(ctx, mount);
}

UTEST(localization, fmt_macro_takes_a_full_eight_arguments) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    ASSERT_TRUE_MSG(fl_string_equals(FL_FMT("SID_fmt_eight", 1, 2.5, "three", FL_S("four"), (i64)5, 6.5f, "seven", 8),
                                     FL_S("1 2.5 three four 5 6.5 seven 8")),
                    "eight mixed-type arguments each land in their own slot");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, plural_macro_selects_the_variant_and_tags_the_rest) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    ASSERT_STRING_CHECKS(STRING_CHECK(FL_PLURAL("SID_file_count", 1), FL_S("1 file"),
                                      "the count alone selects the variant and fills {0}"),
                         STRING_CHECK(FL_PLURAL("SID_file_count", 3), FL_S("3 files"),
                                      "and a count the rules put in another category selects that one"));

    const u32 many = 7;
    ASSERT_STRING_CHECKS(STRING_CHECK(FL_PLURAL("SID_plural_args", many, "Documents"), FL_S("7 files in Documents"),
                                      "arguments after the count shift to {1} onwards and tag by type"),
                         STRING_CHECK(FL_PLURAL("SID_plural_args", 1, FL_S("Photos")), FL_S("1 file in Photos"),
                                      "an FlString argument after the count tags as Str"));

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_macro_result_is_a_widget_text_argument) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    // Wide enough that neither line wraps. The two texts share the row, and a wrapped line would reach the
    // snapshot as several text elements.
    ctx->layout_width = 1024.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1024.0f, .height = 256.0f });

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(resolve_widget_id(FL_S("loc_macro_probe"), string_empty()),
                          .sizing = { sizing_fixed(1000), sizing_fixed(80) }, .color = ui_color(40, 200, 60, 255)) {
            ui_text(FL_FMT("SID_greeting", "Ada"));
            ui_text(FL_PLURAL("SID_file_count", 2));
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    char snapshot[8192];
    fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "Hello, Ada!") != nullptr, "FL_FMT passes inline as the text of a widget");
    ASSERT_TRUE_MSG(strstr(snapshot, "2 files") != nullptr, "FL_PLURAL passes inline as the text of a widget");

    localization_fixture_release(ctx, mount);
}
