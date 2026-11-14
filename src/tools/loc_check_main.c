#include "loc_check.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/json.h>
#include <core/os/os.h>
#include <core/path.h>

#include <stdio.h>
#include <string.h>

// loc_check: reports drift between source code and the localization String Tables. It never writes a table: the
// default table is maintained by hand and carries translator context in its JSON5 comments.

static const char* k_usage
    = "usage: loc_check --manifest <path> [--source <dir-or-file>]...\n"
      "\n"
      "Reports drift between SID literals in source code and the String Tables a Language\n"
      "Manifest enumerates. It only reports: no table is ever written.\n"
      "\n"
      "  --manifest <path>          the Language Manifest to check (required)\n"
      "  --source <dir-or-file>     .c and .rs sources to scan, searched recursively when a\n"
      "                             directory; repeatable. With none given, the two checks\n"
      "                             that compare against code are skipped.\n"
      "  -h, --help                 this text\n"
      "\n"
      "Warnings are drift the runtime's fallback chain absorbs silently; errors are drift the\n"
      "user sees on screen in the default language. A table file that cannot be read or parsed\n"
      "is an error whatever language it belongs to. Exit status is 1 when there are errors,\n"
      "0 otherwise.\n"
      "\n"
      "The code scan matches `\"SID_...` string literals only; a SID assembled at runtime is not\n"
      "seen.\n";

// Whole file into arena. Empty on any failure, which the checker reports as an unreadable table.

static FlString read_file(FlArena* arena, FlString path) {
    const i64 size = file_get_size_os(path);
    if (size <= 0) {
        return string_empty();
    }

    u8* data = arena_alloc_array(arena, u8, (u64)size);
    if (file_read_os(data, path, size) != size) {
        return string_empty();
    }
    return string_from_cstr_len((const char*)data, (u64)size);
}

typedef struct SourceList {
    LocCheckSource* items;
    u32 count;
    u32 capacity;
    FlArena* arena;
} SourceList;

static void source_list_add(SourceList* self, FlString path) {
    const FlString text = read_file(self->arena, path);
    if (string_is_empty(text)) {
        return;
    }

    if (self->count == self->capacity) {
        const u32 capacity = self->capacity == 0 ? 64 : self->capacity * 2;
        LocCheckSource* items = arena_alloc_array_zero(self->arena, LocCheckSource, capacity);
        memcpy(items, self->items, self->count * sizeof(LocCheckSource));
        self->items = items;
        self->capacity = capacity;
    }

    self->items[self->count++] = (LocCheckSource) { .path = path, .text = text };
}

static bool is_source_file(FlString path) {
    return fl_path_has_extension(path, FL_S("c")) || fl_path_has_extension(path, FL_S("rs"));
}

// Hidden entries are build and VCS scaffolding, never checked-in source worth scanning.
static bool is_hidden(FlString name) {
    return name.length == 0 || name.data[0] == '.';
}

static void collect_sources(SourceList* self, FlString path);

static void collect_entry(SourceList* self, FlString directory, FileStat entry) {
    if (is_hidden(entry.name)) {
        return;
    }

    const FlString child = fl_path_join(self->arena, directory, entry.name);
    if (entry.is_directory) {
        collect_sources(self, child);
    } else if (is_source_file(child)) {
        source_list_add(self, child);
    }
}

// Collect the .c and .rs files under path, recursing into subdirectories. A plain file is taken as-is whatever its
// extension, so a single file can be pointed at directly.

static void collect_sources(SourceList* self, FlString path) {
    if (!file_directory_exists_os(path)) {
        source_list_add(self, path);
        return;
    }

    DirHandle dir = dir_open_os(self->arena, path);
    if (!dir.is_valid) {
        return;
    }

    for (FileStat entry = dir_read_next_os(self->arena, &dir); entry.is_valid;
         entry = dir_read_next_os(self->arena, &dir)) {
        collect_entry(self, path, entry);
    }

    dir_close_os(&dir);
}

static bool read_manifest(FlArena* arena, FlString manifest_path, FlString* default_tag, FlJsonArray** languages) {
    const FlString manifest = read_file(arena, manifest_path);
    if (string_is_empty(manifest)) {
        fprintf(stderr, "loc_check: cannot read manifest %.*s\n", (int)manifest_path.length, manifest_path.data);
        return false;
    }

    FlJsonParseResult result = { 0 };
    FlJsonValue* root = fl_json_parse_with_flags(arena, manifest, FlJsonParseFlags_AllowJson5, &result);
    if (root == nullptr) {
        const FlString error = fl_json_get_error_description(result.error);
        fprintf(stderr, "loc_check: manifest %.*s failed to parse: %.*s\n", (int)manifest_path.length,
                manifest_path.data, (int)error.length, error.data);
        return false;
    }

    *default_tag = fl_json_get_string_prop(root, FL_S("default"));
    *languages = fl_json_get_array_prop(root, FL_S("languages"));
    if (string_is_empty(*default_tag) || *languages == nullptr) {
        fprintf(stderr, "loc_check: manifest %.*s needs a \"default\" tag and a \"languages\" array\n",
                (int)manifest_path.length, manifest_path.data);
        return false;
    }
    return true;
}

static u32 count_languages(FlJsonArray* languages) {
    u32 count = 0;
    for_each_json_array_item(languages, entry, index) {
        (void)entry;
        (void)index;
        count++;
    }
    return count;
}

typedef struct TableSet {
    LocCheckTable* tables;
    u32 count;
    u32 next;
    FlString default_tag;
    FlString directory;
} TableSet;

// The default language has to be tables[0]: every cross-table check reads it from there.
static bool add_table(FlArena* arena, TableSet* self, FlJsonValue* entry) {
    const FlString tag = fl_json_get_string_prop(entry, FL_S("tag"));
    const FlString file = fl_json_get_string_prop(entry, FL_S("file"));
    if (string_is_empty(tag) || string_is_empty(file)) {
        fprintf(stderr, "loc_check: manifest entry needs both a \"tag\" and a \"file\"\n");
        return false;
    }

    const bool is_default = fl_string_equals_nocase(tag, self->default_tag);
    if (!is_default && self->next == self->count) {
        fprintf(stderr, "loc_check: manifest names \"%.*s\" as the default but lists no such language\n",
                (int)self->default_tag.length, self->default_tag.data);
        return false;
    }

    const FlString path = fl_path_join(arena, self->directory, file);
    self->tables[is_default ? 0 : self->next++]
        = (LocCheckTable) { .tag = tag, .path = path, .json = read_file(arena, path) };
    return true;
}

// Read the manifest and turn its language list into tables, the default language first. Returns 0 when the manifest
// itself is unusable.

static u32 load_tables(FlArena* arena, FlString manifest_path, LocCheckTable** out_tables) {
    FlString default_tag = string_empty();
    FlJsonArray* languages = nullptr;
    if (!read_manifest(arena, manifest_path, &default_tag, &languages)) {
        return 0;
    }

    const u32 count = count_languages(languages);
    TableSet set = { .tables = arena_alloc_array_zero(arena, LocCheckTable, count),
                     .count = count,
                     .next = 1,
                     .default_tag = default_tag,
                     .directory = fl_path_get_directory(arena, manifest_path) };

    for_each_json_array_item(languages, entry, index) {
        (void)index;
        if (!add_table(arena, &set, entry)) {
            return 0;
        }
    }

    if (set.next != count) {
        fprintf(stderr, "loc_check: manifest lists \"%.*s\" more than once\n", (int)default_tag.length,
                default_tag.data);
        return 0;
    }

    *out_tables = set.tables;
    return count;
}

static bool is_flag(const char* arg, const char* name) {
    return strcmp(arg, name) == 0;
}

static bool is_help_flag(const char* arg) {
    return is_flag(arg, "-h") || is_flag(arg, "--help");
}

// Returns the exit status to stop with, or -1 to carry on and run the check.
static int parse_args(int argc, char** argv, FlString* manifest_path, SourceList* sources) {
    for (int i = 1; i < argc; i++) {
        const bool has_value = i + 1 < argc;
        if (has_value && is_flag(argv[i], "--manifest")) {
            *manifest_path = string_from_cstr(argv[++i]);
        } else if (has_value && is_flag(argv[i], "--source")) {
            collect_sources(sources, string_from_cstr(argv[++i]));
        } else if (is_help_flag(argv[i])) {
            printf("%s", k_usage);
            return 0;
        } else {
            fprintf(stderr, "loc_check: unrecognized argument \"%s\"\n\n%s", argv[i], k_usage);
            return 2;
        }
    }

    if (string_is_empty(*manifest_path)) {
        fprintf(stderr, "%s", k_usage);
        return 2;
    }
    return -1;
}

int main(int argc, char** argv) {
    FlArena* arena = fl_init(0);

    FlString manifest_path = string_empty();
    SourceList sources = { .arena = arena };

    const int exit_status = parse_args(argc, argv, &manifest_path, &sources);
    if (exit_status >= 0) {
        return exit_status;
    }

    LocCheckTable* tables = nullptr;
    const u32 table_count = load_tables(arena, manifest_path, &tables);
    if (table_count == 0) {
        return 2;
    }

    const LocCheckResult result = loc_check_run(arena, &(LocCheckInput) { .sources = sources.items,
                                                                          .source_count = sources.count,
                                                                          .tables = tables,
                                                                          .table_count = table_count });
    const FlString report = loc_check_format_report(arena, &result, tables, table_count);
    printf("%.*s", (int)report.length, report.data);

    return result.error_count != 0 ? 1 : 0;
}
