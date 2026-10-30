#pragma once

#include <core/arena.h>
#include <core/heap.h>
#include <core/string.h>
#include <flowi/core/string.h>
#include <flowi/vfs/vfs_plugin.h>
#include <flowi/vfs/vfs_types.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cached archive entry for directory listing (linked list node)

typedef struct VfsCachedEntry {
    struct VfsCachedEntry* next;
    FlString path;     // Full path within archive (without trailing slash)
    int64_t size;      // File size (0 for directories)
    bool is_directory; // Whether this entry is a directory
} VfsCachedEntry;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Directory listing cache of one archive handle, built on the first listing

#define VFS_ARCHIVE_CACHE_ARENA_SIZE (256 * 1024 * 1024)

typedef struct VfsEntryCache {
    FlArena* arena; // nullptr until the cache is begun
    VfsCachedEntry* head;
    VfsCachedEntry* tail;
} VfsEntryCache;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Starts the cache. False when it was already begun.

static inline bool vfs_entry_cache_begin(VfsEntryCache* cache) {
    if (cache->arena) {
        return false;
    }
    cache->arena = fl_arena_create(VFS_ARCHIVE_CACHE_ARENA_SIZE, __FILE__, __LINE__);
    cache->head = nullptr;
    cache->tail = nullptr;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Appends a copy of path to the cache

static inline void vfs_entry_cache_add(VfsEntryCache* cache, FlString path, int64_t size, bool is_directory) {
    VfsCachedEntry* entry = arena_alloc(cache->arena, VfsCachedEntry);
    entry->path = fl_string_copy(cache->arena, path);
    entry->size = size;
    entry->is_directory = is_directory;
    entry->next = nullptr;

    if (cache->tail) {
        cache->tail->next = entry;
    } else {
        cache->head = entry;
    }
    cache->tail = entry;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State every archive driver's handle starts with

typedef struct VfsArchiveHandle {
    FlString target_path;   // Path within archive (empty for the archive root)
    bool is_directory;      // True for the archive root
    uint8_t* memory_buffer; // Archive bytes (nullptr for a file-backed archive)
    uint64_t memory_size;
    bool owns_memory; // memory_buffer is freed with the handle
    VfsEntryCache entries;
} VfsArchiveHandle;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void vfs_archive_handle_init_root(VfsArchiveHandle* base, const uint8_t* data, uint64_t size) {
    base->target_path = FL_S("");
    base->is_directory = true;
    base->memory_buffer = (uint8_t*)data;
    base->memory_size = size;
    base->owns_memory = false;
    base->entries = (VfsEntryCache) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A member of the parent's archive; it shares the parent's bytes without owning them

static inline void vfs_archive_handle_init_child(VfsArchiveHandle* base, const VfsArchiveHandle* parent,
                                                 FlString path) {
    base->target_path = fl_string_copy_malloc(path);
    base->is_directory = false;
    base->memory_buffer = parent->memory_buffer;
    base->memory_size = parent->memory_size;
    base->owns_memory = false;
    base->entries = (VfsEntryCache) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void vfs_archive_handle_release(VfsArchiveHandle* base) {
    if (base->entries.arena) {
        fl_arena_destroy(base->entries.arena);
    }
    fl_string_free(base->target_path);
    if (base->owns_memory && base->memory_buffer) {
        fl_heap_free(base->memory_buffer);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The explicit path when given, otherwise the handle's own

static inline FlString vfs_archive_resolve_target_path(const VfsArchiveHandle* base, FlString path) {
    return path.length > 0 ? path : base->target_path;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// True when the two paths name the same entry, ignoring one trailing slash

static inline bool vfs_archive_entry_path_matches(const char* entry_path, const char* target_cstr, size_t target_len) {
    if (!entry_path || !target_cstr) {
        return false;
    }

    if (strcmp(entry_path, target_cstr) == 0) {
        return true;
    }

    size_t entry_len = strlen(entry_path);

    // "foo/" matches "foo"
    if (entry_len == target_len + 1 && entry_path[entry_len - 1] == '/'
        && strncmp(entry_path, target_cstr, target_len) == 0) {
        return true;
    }

    // "foo" matches "foo/"
    if (target_len > 0 && target_cstr[target_len - 1] == '/' && entry_len == target_len - 1
        && strncmp(entry_path, target_cstr, target_len - 1) == 0) {
        return true;
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumerates the immediate children of `path` within a cached archive entry list.
//
// Emits each immediate child once (collapsing shared directory prefixes to a single entry) via
// `callback`, consistent with filesystem directory semantics. Passing "." or "" lists the archive
// root. Stops early if the callback returns false.

static inline void vfs_archive_list_children(VfsCachedEntry* entries, FlString path, FlVfsListingCallback callback,
                                             void* user_data) {
    if (fl_string_equals(path, FL_S("."))) {
        path = FL_S("");
    }

    arena_scratch_auto(temp);

    uint64_t entry_count = 0;
    for (VfsCachedEntry* cached = entries; cached; cached = cached->next) {
        entry_count++;
    }

    FlString* seen_names = arena_alloc_array(temp.arena, FlString, entry_count);
    uint64_t seen_count = 0;

    for (VfsCachedEntry* cached = entries; cached; cached = cached->next) {
        FlString full_path = cached->path;

        if (path.length > 0) {
            if (!fl_string_begins_with(full_path, path)) {
                continue;
            }
            if (full_path.length == path.length) {
                continue;
            }
            if (full_path.data[path.length] != '/') {
                continue;
            }
            full_path = fl_string_substr(full_path, path.length + 1, full_path.length - path.length - 1);
        }

        int64_t slash_idx = fl_string_find_char(full_path, '/');
        FlString child_name;
        bool is_dir;

        if (slash_idx >= 0) {
            child_name = fl_string_substr(full_path, 0, (uint64_t)slash_idx);
            is_dir = true;
        } else {
            child_name = full_path;
            is_dir = cached->is_directory;
        }

        if (child_name.length == 0) {
            continue;
        }

        bool already_seen = false;
        for (uint64_t j = 0; j < seen_count; j++) {
            if (fl_string_equals(seen_names[j], child_name)) {
                already_seen = true;
                break;
            }
        }

        if (already_seen) {
            continue;
        }

        seen_names[seen_count++] = fl_string_copy(temp.arena, child_name);

        FlVfsEntry vfs_entry = {
            .name = child_name,
            .is_directory = is_dir,
            .is_archive = false,
            .size = is_dir ? 0 : cached->size,
            .attributes = 0,
        };

        if (!callback(user_data, &vfs_entry)) {
            break;
        }
    }
}
