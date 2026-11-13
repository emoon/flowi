#include "localization_private.h"
#include <core/log.h>

// The worker-side parsers for the manifest and the string tables.

// Runs on a VFS worker thread. Parses the manifest into its own arena and records the tag it names as the
// default language. A failure leaves the Manifest empty, so whatever list is already in force stays untouched.

FlVfsData localization_manifest_load_callback(void* data, i64 size, void* user_data) {
    Manifest* self = (Manifest*)user_data;

    if (data == nullptr || size <= 0) {
        log_warning("Localization manifest could not be read; SID text will render as-is");
        return (FlVfsData) { .success = false };
    }

    FlJsonParseResult result = { 0 };
    FlJsonValue* root = fl_json_parse_with_flags(self->arena, string_from_cstr_len((const char*)data, (u64)size),
                                                 FlJsonParseFlags_AllowJson5, &result);
    if (root == nullptr) {
        log_warning("Localization manifest failed to parse: %S", fl_json_get_error_description(result.error));
        return (FlVfsData) { .success = false };
    }

    FlString default_tag = fl_json_get_string_prop(root, FL_S("default"));
    if (string_is_empty(default_tag)) {
        log_warning("Localization manifest has no \"default\" language tag");
        return (FlVfsData) { .success = false };
    }

    self->root = root;
    self->default_tag = default_tag;
    return (FlVfsData) { .success = true };
}

// A view straight into table memory: no copy, and never a static or known-ASCII string.

static FlString table_text(FlJsonValue* value) {
    FlString text = fl_json_value_as_string(value);
    text.is_static = 0;
    text.is_ascii = 0;
    return text;
}

// The CLDR category a variant object's property name spells, false for a name that is none of the six.

static bool plural_category_from_name(FlString name, LocalizationPluralCategory* out) {
    static const char* const names[LocalizationPlural_Count] = { "zero", "one", "two", "few", "many", "other" };

    for (u32 i = 0; i < LocalizationPlural_Count; i++) {
        if (fl_string_equals(name, string_from_cstr(names[i]))) {
            *out = (LocalizationPluralCategory)i;
            return true;
        }
    }
    return false;
}

// Read a Plural Variant object into out, allocating its variant array in the table's arena. Unknown variant names
// and non-string variant bodies warn and are dropped; false means the object carries no other variant, which takes
// the whole key out of the table.

static bool parse_variants(const StringTable* table, FlString key, FlJsonValue* value, TableEntry* out) {
    FlString* variants = arena_alloc_array_zero(table->arena, FlString, LocalizationPlural_Count);
    bool has_other = false;

    for_each_json_property(value, name, variant) {
        LocalizationPluralCategory category = LocalizationPlural_Other;
        if (!plural_category_from_name(*name, &category)) {
            log_warning("Localization key %S in %S has the unknown plural variant \"%S\"; ignoring it", key,
                        table->path, *name);
            continue;
        }
        if (variant->type != FlJsonType_String) {
            log_warning("Localization key %S in %S has a non-string \"%S\" variant; ignoring it", key, table->path,
                        *name);
            continue;
        }

        variants[category] = table_text(variant);
        has_other = has_other || category == LocalizationPlural_Other;
    }

    if (!has_other) {
        log_warning("Localization key %S in %S has no \"other\" plural variant; skipping it", key, table->path);
        return false;
    }

    out->text = variants[LocalizationPlural_Other];
    out->variants = variants;
    return true;
}

// Read one key's value into out: a plain string, or a Plural Variant object. False when the value is neither, or
// the variant object is unusable, so the key falls through the missing-key path.

static bool parse_entry(const StringTable* table, FlString key, FlJsonValue* value, TableEntry* out) {
    if (value->type == FlJsonType_String) {
        out->text = table_text(value);
        return true;
    }

    if (value->type != FlJsonType_Object) {
        log_warning("Localization key %S in %S is neither a string nor a plural-variant object; skipping it", key,
                    table->path);
        return false;
    }

    return parse_variants(table, key, value, out);
}

// Runs on a VFS worker thread. Parses the string table into its own arena and builds the lookup map in the same
// arena.

FlVfsData localization_table_load_callback(void* data, i64 size, void* user_data) {
    StringTable* table = (StringTable*)user_data;

    if (data == nullptr || size <= 0) {
        log_warning("Localization table %S could not be read", table->path);
        return (FlVfsData) { .success = false };
    }

    FlJsonParseResult result = { 0 };
    FlJsonValue* root = fl_json_parse_with_flags(table->arena, string_from_cstr_len((const char*)data, (u64)size),
                                                 FlJsonParseFlags_AllowJson5, &result);
    if (root == nullptr) {
        log_warning("Localization table %S failed to parse: %S", table->path,
                    fl_json_get_error_description(result.error));
        return (FlVfsData) { .success = false };
    }

    FlJsonValue* strings = fl_json_get_object_prop(root, FL_S("strings"));
    if (strings == nullptr) {
        log_warning("Localization table %S has no \"strings\" object", table->path);
        return (FlVfsData) { .success = false };
    }

    for_each_json_property(strings, name, value) {
        TableEntry entry = { 0 };
        if (parse_entry(table, *name, value, &entry)) {
            hashmap_string_insert(&table->strings, *name, entry);
        }
    }

    return (FlVfsData) { .success = true };
}
