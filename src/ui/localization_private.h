#pragma once

#include <core/arena.h>
#include <core/hashmap.h>
#include <core/json.h>
#include <core/string.h>
#include <core/types.h>
#include <flowi/ui/localization.h>
#include <vfs/vfs.h>
#include "localization.h"

// Types and helpers shared by the localization sources. localization.h stays the interface the rest of the UI sees.

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

// VFS read callbacks, run on a worker thread: parse the file into the arena of the Manifest or StringTable passed
// as user_data.
FlVfsData localization_manifest_load_callback(void* data, i64 size, void* user_data);
FlVfsData localization_table_load_callback(void* data, i64 size, void* user_data);

// The positional arguments of one format call.
typedef struct FmtArgs {
    const FlFmtArg* items;
    u64 count;
} FmtArgs;

// Single pass over text into arena: `{<index>}` becomes the argument at that index, and anything else - a named
// slot, an index past the end of args, an unterminated slot - is copied through as written so the gap is visible
// on screen. out_unmatched reports whether any slot found no argument.
FlString localization_substitute(FlArena* arena, FlString text, FmtArgs args, bool* out_unmatched);

// Which category count selects in tag's language, written to out. Returns whether the language has built-in plural
// rules; without any, out is other for every count.
bool localization_select_plural(FlString tag, i64 count, LocalizationPluralCategory* out);
