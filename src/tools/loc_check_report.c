#include "loc_check.h"
#include <core/arena.h>

// How a check titles its group in the report: a label, and whether the language tag follows it.
typedef struct Heading {
    const char* label;
    bool names_language;
} Heading;

static const Heading headings[LocCheckId_Count] = {
    [LocCheckId_MissingFromDefault] = { "missing from the default table", false },
    [LocCheckId_Orphan] = { "orphaned", false },
    [LocCheckId_MissingFromTranslation] = { "missing in", true },
    [LocCheckId_Stale] = { "stale in", true },
    [LocCheckId_PlaceholderArity] = { "placeholder arity", false },
    [LocCheckId_ValueShape] = { "entry shape", false },
    [LocCheckId_TableUnreadable] = { "table unreadable", false },
};

static FlStringBuilder append_heading(FlStringBuilder sb, LocCheckId check, FlString tag, u32 count) {
    const Heading* heading = &headings[check];
    sb = fl_sb_appendf(sb, "  %s", heading->label);
    if (heading->names_language) {
        sb = fl_sb_appendf(sb, " %S", tag);
    }
    return fl_sb_appendf(sb, " (%u)\n", count);
}

static bool finding_is(const LocCheckFinding* finding, u32 language, u32 check) {
    return finding->language == language && (u32)finding->check == check;
}

static u32 count_findings(const LocCheckResult* result, u32 language, u32 check) {
    u32 count = 0;
    for (const LocCheckFinding* f = result->first; f != nullptr; f = f->next) {
        if (finding_is(f, language, check)) {
            count++;
        }
    }
    return count;
}

static FlStringBuilder append_findings(FlStringBuilder sb, const LocCheckResult* result, u32 language, u32 check) {
    for (const LocCheckFinding* f = result->first; f != nullptr; f = f->next) {
        if (finding_is(f, language, check)) {
            sb = fl_sb_appendf(sb, "    %s %S\n", f->is_error ? "error  " : "warning", f->message);
        }
    }
    return sb;
}

// One language's block: its tag line, then a heading and the findings for each check that fired. Nothing at all
// when no check fired.

static FlStringBuilder append_language(FlStringBuilder sb, const LocCheckResult* result, const LocCheckTable* tables,
                                       u32 language) {
    bool printed_tag = false;

    for (u32 check = 0; check < LocCheckId_Count; check++) {
        const u32 count = count_findings(result, language, check);
        if (count == 0) {
            continue;
        }

        if (!printed_tag) {
            sb = fl_sb_appendf(sb, language == 0 ? "%S (default)\n" : "%S\n", tables[language].tag);
            printed_tag = true;
        }
        sb = append_heading(sb, (LocCheckId)check, tables[language].tag, count);
        sb = append_findings(sb, result, language, check);
    }
    return sb;
}

static const char* plural_suffix(u32 count) {
    return count == 1 ? "" : "s";
}

FlString loc_check_format_report(FlArena* arena, const LocCheckResult* result, const LocCheckTable* tables,
                                 u32 table_count) {
    FlStringBuilder sb = fl_sb_create(arena);

    for (u32 language = 0; language < table_count; language++) {
        sb = append_language(sb, result, tables, language);
    }

    if (result->error_count != 0 || result->warning_count != 0) {
        sb = fl_sb_append_char(sb, '\n');
    }
    return fl_sb_to_string(fl_sb_appendf(sb, "%u error%s, %u warning%s\n", result->error_count,
                                         plural_suffix(result->error_count), result->warning_count,
                                         plural_suffix(result->warning_count)));
}
