#include "loc_check.h"
#include "loc_check_scan.h"
#include "loc_check_slots.h"
#include <core/arena.h>
#include <core/hashmap.h>
#include <core/json.h>

// A table after parsing: the strings object in file order for reporting, and the same entries by key for lookup.
// strings is nullptr when the file never got that far, which takes the table out of every cross-table check.
typedef struct ParsedTable {
    FlJsonValue* strings;
    hashmap(FlString, FlJsonValue*) lookup;
} ParsedTable;

typedef struct Checker {
    FlArena* arena;
    LocCheckResult result;
    LocCheckFinding* tail;
} Checker;

static void finding_append(Checker* self, LocCheckFinding finding) {
    LocCheckFinding* added = arena_alloc_zero(self->arena, LocCheckFinding);
    *added = finding;

    if (self->tail == nullptr) {
        self->result.first = added;
    } else {
        self->tail->next = added;
    }
    self->tail = added;

    if (finding.is_error) {
        self->result.error_count++;
    } else {
        self->result.warning_count++;
    }
}

static void finding_error(Checker* self, LocCheckId check, u32 language, FlString message) {
    finding_append(self,
                   (LocCheckFinding) { .check = check, .language = language, .is_error = true, .message = message });
}

static void finding_warning(Checker* self, LocCheckId check, u32 language, FlString message) {
    finding_append(self,
                   (LocCheckFinding) { .check = check, .language = language, .is_error = false, .message = message });
}

// The one variant name every plural object must carry.
static const FlString other_variant = FL_S_("other");

static bool is_cldr_category(FlString name) {
    static const FlString categories[]
        = { FL_S_("zero"), FL_S_("one"), FL_S_("two"), FL_S_("few"), FL_S_("many"), other_variant };

    for (u64 i = 0; i < sizeof_array(categories); i++) {
        if (fl_string_equals(name, categories[i])) {
            return true;
        }
    }
    return false;
}

static void parse_table(Checker* self, const LocCheckTable* table, u32 index, ParsedTable* out) {
    hashmap_string_new(&out->lookup, self->arena, 256);
    FlStringBuilder sb = fl_sb_create(self->arena);

    if (table->json.length == 0) {
        finding_error(self, LocCheckId_TableUnreadable, index,
                      fl_sb_to_string(fl_sb_appendf(sb, "%S is empty or could not be read", table->path)));
        return;
    }

    FlJsonParseResult result = { 0 };
    FlJsonValue* root = fl_json_parse_with_flags(self->arena, table->json, FlJsonParseFlags_AllowJson5, &result);
    if (root == nullptr) {
        finding_error(self, LocCheckId_TableUnreadable, index,
                      fl_sb_to_string(fl_sb_appendf(sb, "%S failed to parse: %S", table->path,
                                                    fl_json_get_error_description(result.error))));
        return;
    }

    FlJsonValue* strings = fl_json_get_object_prop(root, FL_S("strings"));
    if (strings == nullptr) {
        finding_error(self, LocCheckId_TableUnreadable, index,
                      fl_sb_to_string(fl_sb_appendf(sb, "%S has no \"strings\" object", table->path)));
        return;
    }

    out->strings = strings;
    for_each_json_property(strings, name, value) {
        hashmap_string_insert(&out->lookup, *name, value);
    }
}

// The entry a finding is about: its language and key.
typedef struct Entry {
    u32 language;
    FlString name;
} Entry;

// A ValueShape finding is an error in the default table (language 0) and a warning in a translation.
static void shape_finding(Checker* self, u32 language, FlString message) {
    if (language == 0) {
        finding_error(self, LocCheckId_ValueShape, language, message);
    } else {
        finding_warning(self, LocCheckId_ValueShape, language, message);
    }
}

static void check_variant(Checker* self, Entry entry, FlString variant, FlJsonValue* variant_value) {
    if (!is_cldr_category(variant)) {
        shape_finding(self, entry.language,
                      fl_sb_to_string(fl_sb_appendf(fl_sb_create(self->arena), "%S has the non-CLDR variant \"%S\"",
                                                    entry.name, variant)));
    }
    if (variant_value->type != FlJsonType_String) {
        shape_finding(self, entry.language,
                      fl_sb_to_string(fl_sb_appendf(fl_sb_create(self->arena), "%S variant \"%S\" is not a string",
                                                    entry.name, variant)));
    }
}

// LocCheckId_ValueShape for one entry.

static void check_value_shape(Checker* self, Entry entry, FlJsonValue* value) {
    if (value->type == FlJsonType_String) {
        return;
    }
    if (value->type != FlJsonType_Object) {
        shape_finding(
            self, entry.language,
            fl_sb_to_string(fl_sb_appendf(fl_sb_create(self->arena),
                                          "%S is neither a plain string nor a plural-variant object", entry.name)));
        return;
    }

    bool has_other = false;
    for_each_json_property(value, variant, variant_value) {
        has_other |= fl_string_equals(*variant, other_variant);
        check_variant(self, entry, *variant, variant_value);
    }

    if (!has_other) {
        shape_finding(self, entry.language,
                      fl_sb_to_string(fl_sb_appendf(fl_sb_create(self->arena), "%S has no \"%S\" variant", entry.name,
                                                    other_variant)));
    }
}

// LocCheckId_MissingFromDefault and LocCheckId_Orphan: the default table against source code. Skipped when no source
// was scanned.

static void check_against_code(Checker* self, const LocCheckSource* sources, u32 source_count,
                               const ParsedTable* default_table) {
    if (source_count == 0) {
        return;
    }

    SidRef* refs = loc_check_scan_sources(self->arena, sources, source_count);

    hashmap(FlString, u8) used;
    hashmap_string_new(&used, self->arena, 256);

    for (SidRef* ref = refs; ref != nullptr; ref = ref->next) {
        hashmap_string_insert(&used, ref->key, (u8)1);
        if (hashmap_string_get(&default_table->lookup, ref->key) != nullptr) {
            continue;
        }
        finding_error(
            self, LocCheckId_MissingFromDefault, 0,
            fl_sb_to_string(fl_sb_appendf(fl_sb_create(self->arena), "%S:%u: %S", ref->path, ref->line, ref->key)));
    }

    for_each_json_property(default_table->strings, name, value) {
        (void)value;
        if (hashmap_string_get(&used, *name) == nullptr) {
            finding_warning(self, LocCheckId_Orphan, 0, *name);
        }
    }
}

static void check_arity(Checker* self, Entry entry, FlJsonValue* original, FlJsonValue* value) {
    const i64 allowed = loc_check_value_max_slot_index(original);
    const i64 referenced = loc_check_value_max_slot_index(value);
    if (referenced <= allowed) {
        return;
    }

    FlStringBuilder sb = fl_sb_appendf(fl_sb_create(self->arena), "%S references {%lld}, but the default table ",
                                       entry.name, (long long)referenced);
    if (allowed < 0) {
        sb = fl_sb_append_cstr(sb, "uses no placeholders");
    } else {
        sb = fl_sb_appendf(sb, "goes no higher than {%lld}", (long long)allowed);
    }
    finding_error(self, LocCheckId_PlaceholderArity, entry.language, fl_sb_to_string(sb));
}

// LocCheckId_MissingFromTranslation, LocCheckId_Stale and LocCheckId_PlaceholderArity: one translation against the
// default table.

static void check_translation(Checker* self, u32 language, const ParsedTable* translation,
                              const ParsedTable* default_table) {
    for_each_json_property(default_table->strings, name, value) {
        (void)value;
        if (hashmap_string_get(&translation->lookup, *name) == nullptr) {
            finding_warning(self, LocCheckId_MissingFromTranslation, language, *name);
        }
    }

    for_each_json_property(translation->strings, name, value) {
        FlJsonValue** original = hashmap_string_get(&default_table->lookup, *name);
        if (original == nullptr) {
            finding_warning(self, LocCheckId_Stale, language, *name);
            continue;
        }
        check_arity(self, (Entry) { .language = language, .name = *name }, *original, value);
    }
}

static void check_translations(Checker* self, const ParsedTable* parsed, u32 table_count) {
    for (u32 i = 1; i < table_count; i++) {
        if (parsed[i].strings != nullptr) {
            check_translation(self, i, &parsed[i], &parsed[0]);
        }
    }
}

static void check_value_shapes(Checker* self, const ParsedTable* parsed, u32 table_count) {
    for (u32 i = 0; i < table_count; i++) {
        if (parsed[i].strings == nullptr) {
            continue;
        }
        for_each_json_property(parsed[i].strings, name, value) {
            check_value_shape(self, (Entry) { .language = i, .name = *name }, value);
        }
    }
}

LocCheckResult loc_check_run(FlArena* arena, const LocCheckInput* input) {
    Checker self = { .arena = arena };
    if (input->table_count == 0) {
        return self.result;
    }

    ParsedTable* parsed = arena_alloc_array_zero(arena, ParsedTable, input->table_count);
    for (u32 i = 0; i < input->table_count; i++) {
        parse_table(&self, &input->tables[i], i, &parsed[i]);
    }

    if (parsed[0].strings != nullptr) {
        check_against_code(&self, input->sources, input->source_count, &parsed[0]);
        check_translations(&self, parsed, input->table_count);
    }
    check_value_shapes(&self, parsed, input->table_count);

    return self.result;
}
