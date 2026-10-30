#include "loc_check.h"
#include <core/arena.h>
#include <core/hashmap.h>
#include <core/json.h>
#include <ui/localization.h>

#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// One "SID_... literal the scanner found, with where it found it.
typedef struct SidRef {
    struct SidRef* next;
    FlString key;
    FlString path;
    u32 line;
} SidRef;

// A table after parsing: the strings object in file order for reporting, and the same entries by key for lookup.
// strings is null when the file never got that far, which takes the table out of every cross-table check.
typedef struct ParsedTable {
    FlJsonValue* strings;
    hashmap(FlString, FlJsonValue*) lookup;
} ParsedTable;

typedef struct Checker {
    FlArena* arena;
    LocCheckResult result;
    LocCheckFinding* tail;
} Checker;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void finding_add(Checker* self, LocCheckId check, u32 language, bool is_error, FlString message) {
    LocCheckFinding* finding = arena_alloc_zero(self->arena, LocCheckFinding);
    finding->check = check;
    finding->language = language;
    finding->is_error = is_error;
    finding->message = message;

    if (self->tail == nullptr) {
        self->result.first = finding;
    } else {
        self->tail->next = finding;
    }
    self->tail = finding;

    if (is_error) {
        self->result.error_count++;
    } else {
        self->result.warning_count++;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_cldr_category(FlString name) {
    return fl_string_equals(name, FL_S("zero")) || fl_string_equals(name, FL_S("one"))
           || fl_string_equals(name, FL_S("two")) || fl_string_equals(name, FL_S("few"))
           || fl_string_equals(name, FL_S("many")) || fl_string_equals(name, FL_S("other"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Highest {n} argument index text references, or -1 when it references none. Walks the same placeholder
// grammar the runtime substitutes through, so the two cannot disagree about what a slot is.

static i64 max_slot_index(FlString text) {
    i64 highest = -1;
    LocalizationToken token = { 0 };
    u64 cursor = 0;

    while (localization_next_token(text, &cursor, &token)) {
        u32 index = 0;
        if (token.is_slot && localization_slot_index(token.body, &index) && (i64)index > highest) {
            highest = (i64)index;
        }
    }

    return highest;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The same, across every form a value can take: a plural-variant object references whatever its widest variant does.

static i64 value_max_slot_index(FlJsonValue* value) {
    if (value->type == FlJsonType_String) {
        return max_slot_index(fl_json_value_as_string(value));
    }
    if (value->type != FlJsonType_Object) {
        return -1;
    }

    i64 highest = -1;
    for_each_json_property(value, variant, variant_value) {
        (void)variant;
        if (variant_value->type != FlJsonType_String) {
            continue;
        }
        const i64 index = max_slot_index(fl_json_value_as_string(variant_value));
        if (index > highest) {
            highest = index;
        }
    }
    return highest;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Every "SID_... literal in source, appended to the list tail ends. Grep-level: a runtime-assembled SID is not
// seen. The bare "SID_" prefix constant is skipped.

static void scan_source(FlArena* arena, const LocCheckSource* source, SidRef*** tail) {
    const FlString text = source->text;
    u32 line = 1;

    for (u64 i = 0; i < text.length; i++) {
        if (text.data[i] == '\n') {
            line++;
            continue;
        }
        if (text.data[i] != '"' || i + 5 > text.length || memcmp(text.data + i + 1, "SID_", 4) != 0) {
            continue;
        }

        u64 end = i + 5;
        while (end < text.length && text.data[end] != '"' && text.data[end] != '\n') {
            end++;
        }
        if (end == i + 5 || end >= text.length || text.data[end] != '"') {
            continue;
        }

        SidRef* ref = arena_alloc_zero(arena, SidRef);
        ref->key = fl_string_substr(text, i + 1, end - i - 1);
        ref->path = source->path;
        ref->line = line;
        **tail = ref;
        *tail = &ref->next;

        i = end;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void parse_table(Checker* self, const LocCheckTable* table, u32 index, ParsedTable* out) {
    hashmap_string_new(&out->lookup, self->arena, 256);
    FlStringBuilder sb = fl_sb_create(self->arena);

    if (table->json.length == 0) {
        finding_add(self, LocCheckId_TableUnreadable, index, true,
                    fl_sb_to_string(fl_sb_appendf(sb, "%S is empty or could not be read", table->path)));
        return;
    }

    FlJsonParseResult result = { 0 };
    FlJsonValue* root = fl_json_parse_with_flags(self->arena, table->json.data, table->json.length,
                                                 FlJsonParseFlags_AllowJson5, &result);
    if (root == nullptr) {
        finding_add(self, LocCheckId_TableUnreadable, index, true,
                    fl_sb_to_string(fl_sb_appendf(sb, "%S failed to parse: %S", table->path,
                                                  fl_json_get_error_description(result.error))));
        return;
    }

    FlJsonValue* strings = fl_json_get_object_prop(root, FL_S("strings"));
    if (strings == nullptr) {
        finding_add(self, LocCheckId_TableUnreadable, index, true,
                    fl_sb_to_string(fl_sb_appendf(sb, "%S has no \"strings\" object", table->path)));
        return;
    }

    out->strings = strings;
    for_each_json_property(strings, name, value) {
        hashmap_string_insert(&out->lookup, *name, value);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LocCheckId_ValueShape: an error in the default table, a warning in a translation.

static void check_value_shape(Checker* self, u32 language, FlString name, FlJsonValue* value) {
    const bool is_error = language == 0;

    if (value->type == FlJsonType_String) {
        return;
    }
    if (value->type != FlJsonType_Object) {
        finding_add(self, LocCheckId_ValueShape, language, is_error,
                    fl_sb_to_string(fl_sb_appendf(fl_sb_create(self->arena),
                                                  "%S is neither a plain string nor a plural-variant object", name)));
        return;
    }

    bool has_other = false;
    for_each_json_property(value, variant, variant_value) {
        if (fl_string_equals(*variant, FL_S("other"))) {
            has_other = true;
        } else if (!is_cldr_category(*variant)) {
            finding_add(self, LocCheckId_ValueShape, language, is_error,
                        fl_sb_to_string(fl_sb_appendf(fl_sb_create(self->arena), "%S has the non-CLDR variant \"%S\"",
                                                      name, *variant)));
        }
        if (variant_value->type != FlJsonType_String) {
            finding_add(self, LocCheckId_ValueShape, language, is_error,
                        fl_sb_to_string(fl_sb_appendf(fl_sb_create(self->arena), "%S variant \"%S\" is not a string",
                                                      name, *variant)));
        }
    }

    if (!has_other) {
        finding_add(self, LocCheckId_ValueShape, language, is_error,
                    fl_sb_to_string(fl_sb_appendf(fl_sb_create(self->arena), "%S has no \"other\" variant", name)));
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LocCheckId_MissingFromDefault and LocCheckId_Orphan: the default table against source code. Skipped when no source
// was scanned.

static void check_against_code(Checker* self, const LocCheckSource* sources, u32 source_count,
                               const ParsedTable* default_table) {
    if (source_count == 0) {
        return;
    }

    SidRef* refs = nullptr;
    SidRef** tail = &refs;
    for (u32 i = 0; i < source_count; i++) {
        scan_source(self->arena, &sources[i], &tail);
    }

    hashmap(FlString, u8) used;
    hashmap_string_new(&used, self->arena, 256);

    for (SidRef* ref = refs; ref != nullptr; ref = ref->next) {
        hashmap_string_insert(&used, ref->key, (u8)1);
        if (hashmap_string_get(&default_table->lookup, ref->key) != nullptr) {
            continue;
        }
        finding_add(
            self, LocCheckId_MissingFromDefault, 0, true,
            fl_sb_to_string(fl_sb_appendf(fl_sb_create(self->arena), "%S:%u: %S", ref->path, ref->line, ref->key)));
    }

    for_each_json_property(default_table->strings, name, value) {
        (void)value;
        if (hashmap_string_get(&used, *name) == nullptr) {
            finding_add(self, LocCheckId_Orphan, 0, false, *name);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LocCheckId_MissingFromTranslation, LocCheckId_Stale and LocCheckId_PlaceholderArity: one translation against the
// default table.

static void check_translation(Checker* self, u32 language, const ParsedTable* translation,
                              const ParsedTable* default_table) {
    for_each_json_property(default_table->strings, name, value) {
        (void)value;
        if (hashmap_string_get(&translation->lookup, *name) == nullptr) {
            finding_add(self, LocCheckId_MissingFromTranslation, language, false, *name);
        }
    }

    for_each_json_property(translation->strings, name, value) {
        FlJsonValue** original = hashmap_string_get(&default_table->lookup, *name);
        if (original == nullptr) {
            finding_add(self, LocCheckId_Stale, language, false, *name);
            continue;
        }

        const i64 allowed = value_max_slot_index(*original);
        const i64 referenced = value_max_slot_index(value);
        if (referenced <= allowed) {
            continue;
        }

        FlStringBuilder sb = fl_sb_appendf(fl_sb_create(self->arena), "%S references {%lld}, but the default table ",
                                           *name, (long long)referenced);
        if (allowed < 0) {
            sb = fl_sb_append_cstr(sb, "uses no placeholders");
        } else {
            sb = fl_sb_appendf(sb, "goes no higher than {%lld}", (long long)allowed);
        }
        finding_add(self, LocCheckId_PlaceholderArity, language, true, fl_sb_to_string(sb));
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

LocCheckResult loc_check_run(FlArena* arena, const LocCheckSource* sources, u32 source_count,
                             const LocCheckTable* tables, u32 table_count) {
    Checker self = { .arena = arena };
    if (table_count == 0) {
        return self.result;
    }

    ParsedTable* parsed = arena_alloc_array_zero(arena, ParsedTable, table_count);
    for (u32 i = 0; i < table_count; i++) {
        parse_table(&self, &tables[i], i, &parsed[i]);
    }

    if (parsed[0].strings != nullptr) {
        check_against_code(&self, sources, source_count, &parsed[0]);

        for (u32 i = 1; i < table_count; i++) {
            if (parsed[i].strings != nullptr) {
                check_translation(&self, i, &parsed[i], &parsed[0]);
            }
        }
    }

    for (u32 i = 0; i < table_count; i++) {
        if (parsed[i].strings == nullptr) {
            continue;
        }
        for_each_json_property(parsed[i].strings, name, value) {
            check_value_shape(&self, i, *name, value);
        }
    }

    return self.result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlStringBuilder append_heading(FlStringBuilder sb, LocCheckId check, FlString tag, u32 count) {
    switch (check) {
        case LocCheckId_MissingFromDefault:
            return fl_sb_appendf(sb, "  missing from the default table (%u)\n", count);
        case LocCheckId_Orphan:
            return fl_sb_appendf(sb, "  orphaned (%u)\n", count);
        case LocCheckId_MissingFromTranslation:
            return fl_sb_appendf(sb, "  missing in %S (%u)\n", tag, count);
        case LocCheckId_Stale:
            return fl_sb_appendf(sb, "  stale in %S (%u)\n", tag, count);
        case LocCheckId_PlaceholderArity:
            return fl_sb_appendf(sb, "  placeholder arity (%u)\n", count);
        case LocCheckId_ValueShape:
            return fl_sb_appendf(sb, "  entry shape (%u)\n", count);
        case LocCheckId_TableUnreadable:
            return fl_sb_appendf(sb, "  table unreadable (%u)\n", count);
        case LocCheckId_Count:
            break;
    }
    return sb;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString loc_check_format_report(FlArena* arena, const LocCheckResult* result, const LocCheckTable* tables,
                                 u32 table_count) {
    FlStringBuilder sb = fl_sb_create(arena);

    for (u32 language = 0; language < table_count; language++) {
        bool printed_tag = false;

        for (u32 check = 0; check < LocCheckId_Count; check++) {
            u32 count = 0;
            for (const LocCheckFinding* f = result->first; f != nullptr; f = f->next) {
                if (f->language == language && (u32)f->check == check) {
                    count++;
                }
            }
            if (count == 0) {
                continue;
            }

            if (!printed_tag) {
                sb = fl_sb_appendf(sb, language == 0 ? "%S (default)\n" : "%S\n", tables[language].tag);
                printed_tag = true;
            }
            sb = append_heading(sb, (LocCheckId)check, tables[language].tag, count);

            for (const LocCheckFinding* f = result->first; f != nullptr; f = f->next) {
                if (f->language == language && (u32)f->check == check) {
                    sb = fl_sb_appendf(sb, "    %s %S\n", f->is_error ? "error  " : "warning", f->message);
                }
            }
        }
    }

    if (result->error_count != 0 || result->warning_count != 0) {
        sb = fl_sb_append_char(sb, '\n');
    }
    return fl_sb_to_string(fl_sb_appendf(sb, "%u %s, %u %s\n", result->error_count,
                                         result->error_count == 1 ? "error" : "errors", result->warning_count,
                                         result->warning_count == 1 ? "warning" : "warnings"));
}
