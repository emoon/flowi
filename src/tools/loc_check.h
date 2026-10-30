#pragma once

#include <core/string.h>
#include <core/types.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Drift checker between source code and the localization String Tables. The engine takes file contents already in
// memory and returns findings; it does no file IO.

typedef enum LocCheckId {
    // A SID literal in code with no entry in the default table.
    LocCheckId_MissingFromDefault,
    // A default-table key no source literal names.
    LocCheckId_Orphan,
    // A default-table key a translation has not picked up yet.
    LocCheckId_MissingFromTranslation,
    // A translation key the default table no longer carries.
    LocCheckId_Stale,
    // A translation slot index above the highest the default's text uses.
    LocCheckId_PlaceholderArity,
    // A value that is neither a plain string nor a well-formed plural-variant object.
    LocCheckId_ValueShape,
    // A table file that is absent, empty, unparseable, or carries no "strings" object. An error in every language;
    // the table is excluded from the cross-table checks.
    LocCheckId_TableUnreadable,
    LocCheckId_Count,
} LocCheckId;

typedef struct LocCheckFinding {
    struct LocCheckFinding* next;
    LocCheckId check;
    // Index into the tables array the finding is reported under.
    u32 language;
    bool is_error;
    // One rendered line, without severity or indentation: the report adds those.
    FlString message;
} LocCheckFinding;

// One language as the manifest named it, plus the raw bytes of its file. An empty json is how the caller reports
// a file it could not read.
typedef struct LocCheckTable {
    FlString tag;
    FlString path;
    FlString json;
} LocCheckTable;

typedef struct LocCheckSource {
    FlString path;
    FlString text;
} LocCheckSource;

typedef struct LocCheckResult {
    LocCheckFinding* first;
    u32 error_count;
    u32 warning_count;
} LocCheckResult;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Run every LocCheckId check. `tables[0]` is the default language; the rest are translations. Findings come back in
// check order per language, and everything they point at is allocated in arena.

LocCheckResult loc_check_run(struct FlArena* arena, const LocCheckSource* sources, u32 source_count,
                             const LocCheckTable* tables, u32 table_count);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render the findings as the human-readable report: grouped per language, then per check, ending in a summary line.

FlString loc_check_format_report(struct FlArena* arena, const LocCheckResult* result, const LocCheckTable* tables,
                                 u32 table_count);
