#include "localization.h"
#include <core/arena.h>
#include <core/file_watcher.h>
#include <core/hashmap.h>
#include <core/json.h>
#include <core/log.h>
#include <core/path.h>
#include <vfs/vfs.h>
#include <flowi/ui/localization.h>
#include "ui_private.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Localization: manifest + string-table loading and SID lookup. Widget text that starts with SID_ is resolved
// here on its way into the display path (ui_text_styled_impl).

// Longest language tag fl_localization_set_language accepts; BCP-47 tags are far shorter in practice.
#define LOCALIZATION_TAG_MAX 64

typedef enum LocalizationState {
    LocalizationState_ManifestLoading,
    LocalizationState_DefaultLoading,
    LocalizationState_Ready,
    LocalizationState_Disabled,
} LocalizationState;

// One key's text. variants is null for a plain string and otherwise points at LocalizationPlural_Count entries,
// of which only other is guaranteed to be set. text is the plain string, or the other variant - what a
// lookup with no count in hand resolves to either way.
typedef struct TableEntry {
    FlString text;
    FlString* variants;
} TableEntry;

// One language's strings. The JSON DOM and the lookup map both live in arena, so retiring a table is exactly
// one fl_arena_destroy. While handle is valid a VFS worker owns the arena; the main thread may read the table only
// once fl_vfs_is_ready reports the read finished, which is what orders the worker's writes before those reads.
typedef struct StringTable {
    FlArena* arena;
    FlString path;
    FlString tag;
    FlVfsHandle handle;
    hashmap(FlString, TableEntry) strings;
    bool warned_no_plural_rules;
} StringTable;

// The language list, in its own arena so a hot-reload re-read can be parsed on a worker and swapped in whole.
// Same arena ownership rule as StringTable above.
typedef struct Manifest {
    FlArena* arena;
    FlJsonValue* root;
    FlString default_tag;
    FlVfsHandle handle;
} Manifest;

typedef struct Localization {
    // App-lifetime: the already-warned key set and the copy of the manifest path.
    FlArena* arena;
    FlVfsMount* mount;
    FlString manifest_path;

    // The language list in force, and a re-read of it started by hot reload or still in flight at startup.
    Manifest* manifest;
    Manifest* pending_manifest;

    // The only two tables ever resident. They are the same pointer while the active language is the default.
    StringTable* active_table;
    StringTable* default_table;

    // The table a runtime switch is loading on a worker, swapped in at the frame boundary once it is ready.
    StringTable* pending_table;

    LocalizationState state;

    // Valid only while hot reload is on; the sole thing that makes a reload happen.
    FlFileWatcherHandle watcher;

    // Recorded by fl_localization_set_language, applied by localization_frame_begin.
    char pending_tag[LOCALIZATION_TAG_MAX];
    u32 pending_tag_length;
    bool has_pending_tag;

    // A reload of the default table while another language is active replaces the fallback layer in place rather
    // than making the default language current. Carried from the recorded tag to the load it starts.
    bool pending_tag_default_only;
    bool pending_default_only;

    // Reloads the watcher has asked for and the frame boundary has not started yet; one flag per resident table.
    //
    // One switch at a time: a tag or reload recorded while another is loading waits for it to land.
    bool reload_active;
    bool reload_default;

    // Reloads the watcher has asked for since creation.
    u64 hot_reload_requests;

    hashmap(FlString, u8) warned;
} Localization;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Find tag in the manifest's language list and return its table path, resolved against the manifest's own
// directory and allocated in arena. Empty when the manifest lists no such tag or the entry names no file.

static FlString manifest_table_path(Localization* self, FlArena* arena, FlString tag) {
    FlJsonArray* languages = fl_json_get_array_prop(self->manifest->root, FL_S("languages"));
    if (languages == nullptr) {
        log_warning("Localization manifest has no \"languages\" array");
        return string_empty();
    }

    for_each_json_array_item(languages, entry, index) {
        (void)index;
        if (!fl_string_equals_nocase(fl_json_get_string_prop(entry, FL_S("tag")), tag)) {
            continue;
        }
        FlString file = fl_json_get_string_prop(entry, FL_S("file"));
        if (string_is_empty(file)) {
            log_warning("Localization manifest entry for %S has no \"file\"", tag);
            return string_empty();
        }
        return fl_path_join(arena, fl_path_get_directory(arena, self->manifest_path), file);
    }

    log_warning("Localization manifest lists no language with the tag %S", tag);
    return string_empty();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Runs on a VFS worker thread. Parses the manifest into its own arena and records the tag it names as the
// default language. A failure leaves the Manifest empty, so whatever list is already in force stays untouched.

static FlVfsData manifest_load_callback(void* data, i64 size, void* user_data) {
    Manifest* self = (Manifest*)user_data;

    if (data == nullptr || size <= 0) {
        log_warning("Localization manifest could not be read; SID text will render as-is");
        return (FlVfsData) { .success = false };
    }

    FlJsonParseResult result = { 0 };
    FlJsonValue* root = fl_json_parse_with_flags(self->arena, data, (size_t)size, FlJsonParseFlags_AllowJson5, &result);
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A view straight into table memory: no copy, and never a static or known-ASCII string.

static FlString table_text(FlJsonValue* value) {
    FlString text = fl_json_value_as_string(value);
    text.is_static = 0;
    text.is_ascii = 0;
    return text;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read a Plural Variant object into out, allocating its variant array in arena. Unknown variant names and
// non-string variant bodies warn and are dropped; false means the object carries no other variant, which takes
// the whole key out of the table.

static bool parse_variants(FlArena* arena, FlString path, FlString key, FlJsonValue* value, TableEntry* out) {
    FlString* variants = arena_alloc_array_zero(arena, FlString, LocalizationPlural_Count);
    bool has_other = false;

    for_each_json_property(value, name, variant) {
        LocalizationPluralCategory category = LocalizationPlural_Other;
        if (!plural_category_from_name(*name, &category)) {
            log_warning("Localization key %S in %S has the unknown plural variant \"%S\"; ignoring it", key, path,
                        *name);
            continue;
        }
        if (variant->type != FlJsonType_String) {
            log_warning("Localization key %S in %S has a non-string \"%S\" variant; ignoring it", key, path, *name);
            continue;
        }

        variants[category] = table_text(variant);
        has_other = has_other || category == LocalizationPlural_Other;
    }

    if (!has_other) {
        log_warning("Localization key %S in %S has no \"other\" plural variant; skipping it", key, path);
        return false;
    }

    out->text = variants[LocalizationPlural_Other];
    out->variants = variants;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Runs on a VFS worker thread. Parses the string table into its own arena and builds the lookup map in the same
// arena. A value that is neither a plain string nor a plural-variant object is skipped so the key falls through
// the missing-key path.

static FlVfsData table_load_callback(void* data, i64 size, void* user_data) {
    StringTable* table = (StringTable*)user_data;

    if (data == nullptr || size <= 0) {
        log_warning("Localization table %S could not be read", table->path);
        return (FlVfsData) { .success = false };
    }

    FlJsonParseResult result = { 0 };
    FlJsonValue* root
        = fl_json_parse_with_flags(table->arena, data, (size_t)size, FlJsonParseFlags_AllowJson5, &result);
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
        if (value->type == FlJsonType_String) {
            entry.text = table_text(value);
        } else if (value->type != FlJsonType_Object) {
            log_warning("Localization key %S in %S is neither a string nor a plural-variant object; skipping it", *name,
                        table->path);
            continue;
        } else if (!parse_variants(table->arena, table->path, *name, value, &entry)) {
            continue;
        }
        hashmap_string_insert(&table->strings, *name, entry);
    }

    return (FlVfsData) { .success = true };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start the worker-side read that parses and builds the table for tag. Returns null when the manifest does not
// list the tag or the read cannot start; the caller's current table stays in place either way.

static StringTable* table_start(Localization* self, FlString tag) {
    FlArena* arena = arena_new();
    StringTable* table = arena_alloc_zero(arena, StringTable);
    table->arena = arena;
    table->tag = fl_string_copy(arena, tag);
    table->path = manifest_table_path(self, arena, tag);
    if (string_is_empty(table->path)) {
        fl_arena_destroy(arena);
        return nullptr;
    }
    hashmap_string_new(&table->strings, arena, 256);

    table->handle = vfs_mount_read_all(self->mount, table->path, .callback = table_load_callback, .user_data = table);
    if (table->handle == FL_VFS_HANDLE_INVALID) {
        log_warning("Localization could not start reading table %S", table->path);
        fl_arena_destroy(arena);
        return nullptr;
    }

    return table;
}

// How far a read has got. The arena behind it is the caller's to keep or retire once this leaves
// TableStatus_Loading; before that the worker still owns it.

typedef enum TableStatus {
    TableStatus_Loading,
    TableStatus_Ready,
    TableStatus_Failed,
} TableStatus;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Poll one in-flight read, closing its handle as soon as the worker is done with it.

static TableStatus load_poll(FlVfsHandle* handle) {
    if (!fl_vfs_is_ready(*handle)) {
        return TableStatus_Loading;
    }

    const bool ok = fl_vfs_get_data(*handle).success;
    fl_vfs_close(*handle);
    *handle = FL_VFS_HANDLE_INVALID;
    return ok ? TableStatus_Ready : TableStatus_Failed;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start the worker-side read of the manifest into an arena of its own. Null when the read cannot start.

static Manifest* manifest_start(Localization* self) {
    FlArena* arena = arena_new();
    Manifest* manifest = arena_alloc_zero(arena, Manifest);
    manifest->arena = arena;

    manifest->handle = vfs_mount_read_all(self->mount, self->manifest_path, .callback = manifest_load_callback,
                                          .user_data = manifest);
    if (manifest->handle == FL_VFS_HANDLE_INVALID) {
        fl_arena_destroy(arena);
        return nullptr;
    }

    return manifest;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destroy a replaced table's arena unless it is one of the two tables still in use.

static void retire_table(StringTable* table, const StringTable* keep_a, const StringTable* keep_b) {
    if (table != nullptr && table != keep_a && table != keep_b) {
        fl_arena_destroy(table->arena);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Make table the active language and retire the table it replaced, unless that is the default one, which stays
// resident as the fallback layer. A null table is a failed load, which leaves the current language alone.

static void switch_to_table(Localization* self, StringTable* table, FlString tag) {
    if (table == nullptr) {
        log_warning("Localization could not switch to %S; staying on the current language", tag);
        return;
    }

    StringTable* old_active = self->active_table;
    StringTable* old_default = self->default_table;

    self->active_table = table;
    if (fl_string_equals_nocase(tag, self->manifest->default_tag)) {
        self->default_table = table;
    }

    retire_table(old_active, self->active_table, self->default_table);
    retire_table(old_default, self->default_table, old_active);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The tag fl_localization_set_language recorded, as a view of the pending-tag buffer.

static FlString pending_tag_string(Localization* self) {
    return string_from_cstr_len(self->pending_tag, self->pending_tag_length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame-boundary step for a manifest read: a no-op until the worker is done with it. The first one brings
// localization up - a manifest that cannot be read or names no default disables it instead - and every later one
// is a hot-reload re-read, which swaps the language list in and keeps the last good one if it did not parse.

static void manifest_poll(Localization* self) {
    const TableStatus status = load_poll(&self->pending_manifest->handle);
    if (status == TableStatus_Loading) {
        return;
    }

    Manifest* manifest = self->pending_manifest;
    const bool is_reload = self->manifest != nullptr;
    self->pending_manifest = nullptr;

    if (status == TableStatus_Failed) {
        fl_arena_destroy(manifest->arena);
        if (is_reload) {
            log_warning("Localization manifest reload failed; keeping the language list already in force");
        } else {
            self->state = LocalizationState_Disabled;
        }
        return;
    }

    if (is_reload) {
        fl_arena_destroy(self->manifest->arena);
        self->manifest = manifest;
        return;
    }

    self->manifest = manifest;
    self->pending_table = table_start(self, manifest->default_tag);
    self->state = self->pending_table != nullptr ? LocalizationState_DefaultLoading : LocalizationState_Ready;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame-boundary step for the default table: it becomes the active language on the boundary the worker finishes
// it, and a read that failed leaves the run with no fallback layer. Either way localization is ready afterwards,
// so a language a caller asked for before this point can start loading.

static void default_poll(Localization* self) {
    const TableStatus status = load_poll(&self->pending_table->handle);
    if (status == TableStatus_Loading) {
        return;
    }

    StringTable* table = self->pending_table;
    self->pending_table = nullptr;
    self->state = LocalizationState_Ready;

    if (status == TableStatus_Failed) {
        fl_arena_destroy(table->arena);
        return;
    }

    self->default_table = table;
    self->active_table = table;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Swap in the table a runtime switch is loading, once the worker is done with it. Until then the call is a no-op
// and frames keep resolving against the language that is already active. A reload of the default table while
// another language is active only replaces the fallback layer, leaving the active language where it is.

static void finish_pending_switch(Localization* self) {
    StringTable* table = self->pending_table;
    const TableStatus status = load_poll(&table->handle);
    if (status == TableStatus_Loading) {
        return;
    }

    self->pending_table = nullptr;
    const bool default_only = self->pending_default_only;
    self->pending_default_only = false;

    if (!default_only) {
        switch_to_table(self, status == TableStatus_Ready ? table : nullptr, table->tag);
        if (status == TableStatus_Failed) {
            fl_arena_destroy(table->arena);
        }
        return;
    }

    if (status == TableStatus_Failed) {
        log_warning("Localization could not reload the default table %S; keeping the current one", table->path);
        fl_arena_destroy(table->arena);
        return;
    }

    StringTable* old_default = self->default_table;
    self->default_table = table;
    retire_table(old_default, self->active_table, self->default_table);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Record a switch to tag for the next frame boundary. default_only asks for the fallback layer to be
// replaced in place instead of tag becoming the active language. A tag the pending-tag buffer cannot hold is
// rejected.

static void request_tag(Localization* self, FlString tag, bool default_only) {
    if (string_is_empty(tag) || tag.length >= LOCALIZATION_TAG_MAX) {
        log_warning("Localization language tag %S is empty or too long; ignoring it", tag);
        return;
    }

    memcpy(self->pending_tag, tag.data, tag.length);
    self->pending_tag_length = (u32)tag.length;
    self->has_pending_tag = true;
    self->pending_tag_default_only = default_only;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The watcher reports paths under the mount's source directory; every path localization holds is mount-relative.
// Empty for a path that is not under the mount at all.

static FlString mount_relative_path(FlString change_path, FlString source_path) {
    if (!fl_string_begins_with(change_path, source_path)) {
        return string_empty();
    }

    u64 offset = source_path.length;
    while (offset < change_path.length && (change_path.data[offset] == '/' || change_path.data[offset] == '\\')) {
        offset++;
    }
    return fl_string_substr(change_path, offset, change_path.length - offset);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Watch the directory the manifest lives in, so hot reload sees edits to it and to the tables it names.

static void watcher_start(Localization* self) {
    arena_scratch_auto(temp);

    FlString source = fl_vfs_mount_get_info(self->mount).path;
    FlString dir = fl_path_join(temp.arena, source, fl_path_get_directory(temp.arena, self->manifest_path));

    const FlFileWatcherConfig config
        = { .recursive = true, .watch_files = true, .watch_directories = false, .max_changes = 0 };

    self->watcher = file_watcher_start(dir, config);
    if (self->watcher == FL_FILE_WATCHER_INVALID) {
        log_warning("Localization could not watch %S; hot reload is off for this run", dir);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hot reload. Frame-boundary drain of the watcher: a resident table's file records a reload of that table, and the
// manifest's file starts a re-read of the language list. The drain sets the reload flags rather than switching
// immediately (see the reload flags on Localization). No-op without a watcher.

static void watcher_poll(Localization* self) {
    if (!file_watcher_has_changes(self->watcher)) {
        return;
    }

    arena_scratch_auto(temp);

    const FlString source = fl_vfs_mount_get_info(self->mount).path;
    FileChangeIterator iter = file_watcher_get_changes(self->watcher, temp.arena);

    while (file_change_iterator_has_next(&iter)) {
        const FileChange change = file_change_iterator_next(&iter);
        const u32 content_change = FlFileChangeType_Created | FlFileChangeType_Modified;
        if (change.is_directory || (change.change_types & content_change) == 0) {
            continue;
        }

        const FlString path = mount_relative_path(change.path, source);
        if (fl_string_equals(path, self->manifest_path)) {
            self->hot_reload_requests++;
            if (self->pending_manifest == nullptr) {
                self->pending_manifest = manifest_start(self);
            }
            continue;
        }

        if (self->active_table != nullptr && fl_string_equals(path, self->active_table->path)) {
            self->reload_active = true;
            self->hot_reload_requests++;
        } else if (self->default_table != nullptr && fl_string_equals(path, self->default_table->path)) {
            self->reload_default = true;
            self->hot_reload_requests++;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Turn one recorded reload into the switch that carries it out, active table first. A reload of the default table
// while another language is active replaces the fallback layer, so it goes in as a default-only switch.

static void start_recorded_reload(Localization* self) {
    if (self->reload_active) {
        self->reload_active = false;
        if (self->active_table != nullptr) {
            request_tag(self, self->active_table->tag, false);
            return;
        }
    }

    if (self->reload_default) {
        self->reload_default = false;
        if (self->default_table != nullptr) {
            request_tag(self, self->default_table->tag, true);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Localization* localization_create(FlVfsMount* mount, FlString manifest_path, bool hot_reload) {
    FlArena* arena = arena_new();
    Localization* self = arena_alloc_zero(arena, Localization);
    self->arena = arena;
    self->mount = mount;
    self->manifest_path = fl_string_copy(arena, manifest_path);
    self->state = LocalizationState_ManifestLoading;
    hashmap_string_new(&self->warned, arena, 64);

    self->pending_manifest = manifest_start(self);
    if (self->pending_manifest == nullptr) {
        log_warning("Localization could not start reading manifest %S; SID text will render as-is", manifest_path);
        self->state = LocalizationState_Disabled;
    }

    if (hot_reload) {
        watcher_start(self);
    }

    return self;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void localization_destroy(Localization* self) {
    if (self == nullptr) {
        return;
    }

    file_watcher_stop(self->watcher);
    self->watcher = FL_FILE_WATCHER_INVALID;
    if (self->pending_manifest != nullptr) {
        // fl_vfs_close does not wait for a running read, and the worker parses into the arena destroyed below.
        fl_vfs_wait(self->pending_manifest->handle);
        fl_vfs_close(self->pending_manifest->handle);
        fl_arena_destroy(self->pending_manifest->arena);
        self->pending_manifest = nullptr;
    }
    if (self->manifest != nullptr) {
        fl_arena_destroy(self->manifest->arena);
        self->manifest = nullptr;
    }
    if (self->pending_table != nullptr) {
        // fl_vfs_close does not wait for a running read, and the worker parses into the arena destroyed below.
        fl_vfs_wait(self->pending_table->handle);
        fl_vfs_close(self->pending_table->handle);
        fl_arena_destroy(self->pending_table->arena);
        self->pending_table = nullptr;
    }
    if (self->active_table != nullptr && self->active_table != self->default_table) {
        fl_arena_destroy(self->active_table->arena);
    }
    if (self->default_table != nullptr) {
        fl_arena_destroy(self->default_table->arena);
    }
    fl_arena_destroy(self->arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void localization_frame_begin(Localization* self) {
    if (self == nullptr) {
        return;
    }

    watcher_poll(self);

    if (self->pending_manifest != nullptr) {
        manifest_poll(self);
    }
    if (self->state == LocalizationState_DefaultLoading) {
        default_poll(self);
    }
    if (self->state != LocalizationState_Ready) {
        // A pending tag survives until the manifest resolves; it is dropped only when localization is disabled.
        self->has_pending_tag = self->has_pending_tag && self->state != LocalizationState_Disabled;
        return;
    }

    if (self->pending_table != nullptr) {
        finish_pending_switch(self);
    }

    // One switch at a time (see the reload flags on Localization): this tag waits for the in-flight table to land.
    if (self->pending_table != nullptr) {
        return;
    }

    // A tag a caller set outruns a reload the watcher asked for; the reload keeps until the switch has landed.
    if (!self->has_pending_tag) {
        start_recorded_reload(self);
    }
    if (!self->has_pending_tag) {
        return;
    }
    self->has_pending_tag = false;
    self->pending_default_only = self->pending_tag_default_only;

    const FlString tag = pending_tag_string(self);
    self->pending_table = table_start(self, tag);
    if (self->pending_table == nullptr) {
        log_warning("Localization could not switch to %S; staying on the current language", tag);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool localization_is_loading(Localization* self) {
    if (self == nullptr) {
        return false;
    }

    return self->state == LocalizationState_DefaultLoading || self->pending_manifest != nullptr
           || self->pending_table != nullptr || self->has_pending_tag;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 localization_resident_table_count(Localization* self) {
    if (self == nullptr) {
        return 0;
    }

    u32 count = self->active_table != nullptr ? 1 : 0;
    if (self->default_table != nullptr && self->default_table != self->active_table) {
        count++;
    }
    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 localization_hot_reload_request_count(Localization* self) {
    return self != nullptr ? self->hot_reload_requests : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool localization_is_watching(Localization* self) {
    return self != nullptr && self->watcher != FL_FILE_WATCHER_INVALID;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// One bit per reason a key can warn about. The bits latch independently: a key that warned about being absent
// can still warn about its placeholders once it is there.

typedef enum LocalizationWarnFlags {
    LocalizationWarn_MissingKey = 1 << 0,
    LocalizationWarn_UnresolvedPlaceholder = 1 << 1,
    LocalizationWarn_MissingArgument = 1 << 2,
    LocalizationWarn_PluralWithoutCount = 1 << 3,
} LocalizationWarnFlags;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// True the first time sid is worth warning about for reason; a lookup runs per element per frame, so each key
// warns once per reason for the run.

static bool first_warning_for(Localization* self, FlString sid, LocalizationWarnFlags reason) {
    u8* seen = hashmap_string_get(&self->warned, sid);
    if (seen != nullptr) {
        if ((*seen & (u8)reason) != 0) {
            return false;
        }
        *seen |= (u8)reason;
        return true;
    }

    FlString key = fl_string_copy(self->arena, sid);
    u8 first = (u8)reason;
    hashmap_string_insert(&self->warned, key, first);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Active table, then the default table. Null means neither carries the key, which is also what tells a caller the
// text it goes on to use is the caller's own key rather than table memory. out_table names the table the entry
// came from, whose language decides how its plural variants are selected.

static TableEntry* lookup_entry(Localization* self, FlString sid, StringTable** out_table) {
    *out_table = nullptr;
    if (self->state != LocalizationState_Ready) {
        return nullptr;
    }

    StringTable* const candidates[2] = { self->active_table, self->default_table };
    for (u32 i = 0; i < 2; i++) {
        if (candidates[i] == nullptr) {
            continue;
        }
        TableEntry* entry = hashmap_string_get(&candidates[i]->strings, sid);
        if (entry != nullptr) {
            *out_table = candidates[i];
            return entry;
        }
    }

    if (first_warning_for(self, sid, LocalizationWarn_MissingKey)) {
        log_warning("Localization has no entry for %S", sid);
    }

    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A plural key reached without a count: the other variant is the closest thing to right, and the count the
// caller never passed is what makes it a guess, so it warns.

static void warn_if_plural(Localization* self, FlString sid, const TableEntry* entry) {
    if (entry->variants != nullptr && first_warning_for(self, sid, LocalizationWarn_PluralWithoutCount)) {
        log_warning("Localization key %S has plural variants but was resolved without a count; the \"other\" "
                    "variant renders",
                    sid);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Whether text carries a {...} placeholder slot.

static bool has_placeholder(FlString text) {
    LocalizationToken token = { 0 };
    u64 cursor = 0;

    while (localization_next_token(text, &cursor, &token)) {
        if (token.is_slot) {
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString localization_lookup(Localization* self, FlString sid) {
    StringTable* table = nullptr;
    TableEntry* entry = lookup_entry(self, sid, &table);
    if (entry == nullptr) {
        return sid;
    }

    warn_if_plural(self, sid, entry);
    if (has_placeholder(entry->text) && first_warning_for(self, sid, LocalizationWarn_UnresolvedPlaceholder)) {
        log_warning("Localization key %S carries placeholders but was resolved without arguments", sid);
    }

    return entry->text;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_localization_init(FlVfsMount* mount, FlString manifest_path, bool hot_reload) {
    if (mount == nullptr) {
        log_error("fl_localization_init needs a mount");
        return;
    }

    UiShared* shared = g_ui_ctx->shared;
    localization_destroy(shared->localization);
    shared->localization = localization_create(mount, manifest_path, hot_reload);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_localization_set_language(FlString tag) {
    Localization* self = g_ui_ctx->shared->localization;
    if (self == nullptr) {
        log_error("fl_localization_set_language needs fl_localization_init to have run");
        return;
    }

    request_tag(self, tag, false);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Whether kind names an arm of the union. An argument that fails this carries nothing readable, so the slot it
// would have filled is left as written, exactly like a slot with no argument at all.

static bool arg_is_readable(FlFmtArg arg) {
    return arg.kind == FlFmtArgKind_Int || arg.kind == FlFmtArgKind_Float || arg.kind == FlFmtArgKind_Str;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlStringBuilder append_arg(FlStringBuilder sb, FlFmtArg arg) {
    switch (arg.kind) {
        case FlFmtArgKind_Int:
            return fl_sb_appendf(sb, "%lld", (long long)arg.value.int_value);
        case FlFmtArgKind_Float:
            return fl_sb_appendf(sb, "%g", arg.value.float_value);
        case FlFmtArgKind_Str:
            return fl_sb_append_string(sb, arg.value.string_value);
    }

    return sb;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Single pass over text into arena: `{<index>}` becomes the argument at that index, and anything else - a named
// slot, an index past the end of args, an unterminated slot - is copied through as written so the gap is visible
// on screen. out_unmatched reports whether any slot found no argument.

static FlString substitute(FlArena* arena, FlString text, const FlFmtArg* args, u64 count, bool* out_unmatched) {
    *out_unmatched = false;
    FlStringBuilder sb = fl_sb_create(arena);
    LocalizationToken token = { 0 };
    u64 cursor = 0;

    while (localization_next_token(text, &cursor, &token)) {
        u32 index = 0;
        if (token.is_slot && localization_slot_index(token.body, &index) && index < count
            && arg_is_readable(args[index])) {
            sb = append_arg(sb, args[index]);
            continue;
        }

        if (token.is_slot) {
            *out_unmatched = true;
        }
        sb = fl_sb_append_string(sb, token.text);
    }

    return fl_sb_to_string(sb);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Substitute args into text, reporting a slot that found no argument once per key.

static FlString substitute_and_warn(Localization* self, FlArena* arena, FlString sid, FlString text,
                                    const FlFmtArg* args, u64 count) {
    bool unmatched = false;
    FlString result = substitute(arena, text, args, count, &unmatched);

    if (unmatched && self != nullptr && first_warning_for(self, sid, LocalizationWarn_MissingArgument)) {
        log_warning("Localization key %S has a placeholder with no matching argument; it renders as written", sid);
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_localize_fmt(FlString sid, const FlFmtArg* args, u32 count) {
    FlUiCtx* ctx = g_ui_ctx;
    if (ctx == nullptr) {
        log_error("fl_localize_fmt needs a ui context");
        return sid;
    }

    if (args == nullptr) {
        count = 0;
    }

    Localization* self = ctx->shared->localization;
    StringTable* table = nullptr;
    TableEntry* entry = self != nullptr ? lookup_entry(self, sid, &table) : nullptr;
    if (entry != nullptr) {
        warn_if_plural(self, sid, entry);
    }

    const bool found = entry != nullptr;
    const FlString text = found ? entry->text : sid;

    const bool has_brace
        = text.length != 0
          && (memchr(text.data, '{', text.length) != nullptr || memchr(text.data, '}', text.length) != nullptr);
    if (!has_brace) {
        // Table memory outlives the frame, the caller's own key bytes need not, so only the fallback is copied.
        return found ? text : fl_string_copy(ctx->render_frame_allocator, text);
    }

    return substitute_and_warn(self, ctx->render_frame_allocator, sid, text, args, count);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Which of the built-in rule sets a language uses. A language none of them names selects other for every count.

typedef enum PluralRules {
    PluralRules_OneOther,
    PluralRules_ZeroOneOther,
    PluralRules_Slavic,
    PluralRules_OtherOnly,
    PluralRules_None,
} PluralRules;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool tag_is_one_of(FlString tag, const char* const* tags) {
    for (; *tags != nullptr; tags++) {
        if (fl_string_equals_nocase(tag, string_from_cstr(*tags))) {
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Only the tag's primary subtag is matched, so pt-br selects the way pt does. A language none of these lists
// names - Arabic, Polish and Czech among them - has no rules here.

static PluralRules plural_rules_for_tag(FlString tag) {
    static const char* const one_other[] = { "en", "de", "nl", "sv", "da", "no", "nb", "nn", "es",
                                             "it", "ca", "el", "fi", "et", "bg", "hu", "tr", nullptr };
    static const char* const zero_one_other[] = { "fr", "pt", "hi", "fa", nullptr };
    static const char* const slavic[] = { "ru", "uk", "be", "sr", "hr", "bs", nullptr };
    static const char* const other_only[] = { "zh", "ja", "ko", "th", "vi", "id", "ms", nullptr };

    const i64 dash = fl_string_find_char(tag, '-');
    const FlString primary = dash < 0 ? tag : fl_string_substr(tag, 0, (u64)dash);

    if (tag_is_one_of(primary, one_other)) {
        return PluralRules_OneOther;
    }
    if (tag_is_one_of(primary, zero_one_other)) {
        return PluralRules_ZeroOneOther;
    }
    if (tag_is_one_of(primary, slavic)) {
        return PluralRules_Slavic;
    }
    if (tag_is_one_of(primary, other_only)) {
        return PluralRules_OtherOnly;
    }
    return PluralRules_None;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static LocalizationPluralCategory plural_category(PluralRules rules, i64 count) {
    // CLDR selects on the magnitude of the count.
    const u64 n = count < 0 ? -(u64)count : (u64)count;

    switch (rules) {
        case PluralRules_OneOther:
            return n == 1 ? LocalizationPlural_One : LocalizationPlural_Other;

        case PluralRules_ZeroOneOther:
            return n <= 1 ? LocalizationPlural_One : LocalizationPlural_Other;

        case PluralRules_Slavic: {
            const u64 ones = n % 10;
            const u64 tens = n % 100;
            if (ones == 1 && tens != 11) {
                return LocalizationPlural_One;
            }
            if (ones >= 2 && ones <= 4 && (tens < 12 || tens > 14)) {
                return LocalizationPlural_Few;
            }
            return LocalizationPlural_Many;
        }

        case PluralRules_OtherOnly:
        case PluralRules_None:
            break;
    }

    return LocalizationPlural_Other;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

LocalizationPluralCategory localization_plural_category(FlString tag, i64 count) {
    return plural_category(plural_rules_for_tag(tag), count);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The variant count selects from entry, in the language of the table it came from. A category the key does not
// carry, and a key that is a plain string rather than a variant object, both resolve to the text that is there.

static FlString plural_text(StringTable* table, const TableEntry* entry, i64 count) {
    if (entry->variants == nullptr) {
        return entry->text;
    }

    const PluralRules rules = plural_rules_for_tag(table->tag);
    if (rules == PluralRules_None && !table->warned_no_plural_rules) {
        table->warned_no_plural_rules = true;
        log_warning("Localization has no plural rules for %S; every count selects the \"other\" variant", table->tag);
    }

    const FlString variant = entry->variants[plural_category(rules, count)];
    return variant.data != nullptr ? variant : entry->text;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_localize_plural(FlString sid, i64 count, const FlFmtArg* args, u32 arg_count) {
    FlUiCtx* ctx = g_ui_ctx;
    if (ctx == nullptr) {
        log_error("fl_localize_plural needs a ui context");
        return sid;
    }

    if (args == nullptr) {
        arg_count = 0;
    }

    Localization* self = ctx->shared->localization;
    StringTable* table = nullptr;
    TableEntry* entry = self != nullptr ? lookup_entry(self, sid, &table) : nullptr;
    const FlString text = entry != nullptr ? plural_text(table, entry, count) : sid;

    // The count is the {0} argument; the caller's own arguments follow it from {1}.
    const u64 total = (u64)arg_count + 1;
    FlFmtArg* all = arena_alloc_array(ctx->render_frame_allocator, FlFmtArg, total);
    all[0] = (FlFmtArg) { .value = { .int_value = count }, .kind = FlFmtArgKind_Int };
    if (arg_count != 0) {
        memcpy(all + 1, args, arg_count * sizeof(FlFmtArg));
    }

    return substitute_and_warn(self, ctx->render_frame_allocator, sid, text, all, total);
}
