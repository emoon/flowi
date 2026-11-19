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

// Cached archive entry for directory listing (linked list node)

typedef struct VfsCachedEntry {
    struct VfsCachedEntry* next;
    FlString path;     // Full path within archive (without trailing slash)
    int64_t size;      // File size (0 for directories)
    bool is_directory; // Whether this entry is a directory
} VfsCachedEntry;

// Directory listing cache of one archive handle, built on the first listing

#define VFS_ARCHIVE_CACHE_ARENA_SIZE (256 * 1024 * 1024)

typedef struct VfsEntryCache {
    FlArena* arena; // nullptr until the cache is begun
    VfsCachedEntry* head;
    VfsCachedEntry* tail;
} VfsEntryCache;

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

// State every archive driver's handle starts with

typedef struct VfsArchiveHandle {
    FlString target_path;   // Path within archive (empty for the archive root)
    bool is_directory;      // True for the archive root
    uint8_t* memory_buffer; // Archive bytes (nullptr for a file-backed archive)
    uint64_t memory_size;
    bool owns_memory; // memory_buffer is freed with the handle
    VfsEntryCache entries;
} VfsArchiveHandle;

static inline void vfs_archive_handle_init_root(VfsArchiveHandle* base, const uint8_t* data, uint64_t size) {
    base->target_path = FL_S("");
    base->is_directory = true;
    base->memory_buffer = (uint8_t*)data;
    base->memory_size = size;
    base->owns_memory = false;
    base->entries = (VfsEntryCache) { 0 };
}

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

static inline void vfs_archive_handle_release(VfsArchiveHandle* base) {
    if (base->entries.arena) {
        fl_arena_destroy(base->entries.arena);
    }
    fl_string_free(base->target_path);
    if (base->owns_memory && base->memory_buffer) {
        fl_heap_free(base->memory_buffer);
    }
}

// The explicit path when given, otherwise the handle's own

static inline FlString vfs_archive_resolve_target_path(const VfsArchiveHandle* base, FlString path) {
    return path.length > 0 ? path : base->target_path;
}

// True when longer is shorter plus one trailing slash

static inline bool vfs_archive_is_slash_variant(const char* longer, size_t longer_len, const char* shorter,
                                                size_t shorter_len) {
    if (longer_len != shorter_len + 1) {
        return false;
    }

    if (longer[longer_len - 1] != '/') {
        return false;
    }

    return strncmp(longer, shorter, shorter_len) == 0;
}

// True when the two paths name the same entry, ignoring one trailing slash

static inline bool vfs_archive_entry_path_matches(const char* entry_path, const char* target_cstr, size_t target_len) {
    if (!entry_path || !target_cstr) {
        return false;
    }

    if (strcmp(entry_path, target_cstr) == 0) {
        return true;
    }

    const size_t entry_len = strlen(entry_path);

    return vfs_archive_is_slash_variant(entry_path, entry_len, target_cstr, target_len)
           || vfs_archive_is_slash_variant(target_cstr, target_len, entry_path, entry_len);
}

static inline uint64_t vfs_cached_entry_count(const VfsCachedEntry* entries) {
    uint64_t count = 0;
    for (const VfsCachedEntry* cached = entries; cached; cached = cached->next) {
        count++;
    }
    return count;
}

// Sets relative to full_path below dir (an empty dir is the root). False when full_path is not strictly below dir.

static inline bool vfs_archive_relative_to(FlString full_path, FlString dir, FlString* relative) {
    if (dir.length == 0) {
        *relative = full_path;
        return true;
    }

    if (!fl_string_begins_with(full_path, dir)) {
        return false;
    }

    if (full_path.length == dir.length) {
        return false;
    }

    if (full_path.data[dir.length] != '/') {
        return false;
    }

    *relative = fl_string_substr(full_path, dir.length + 1, full_path.length - dir.length - 1);
    return true;
}

// The first segment of relative is the child; one with more segments below it is a directory.

static inline FlString vfs_archive_child_name(FlString relative, bool entry_is_directory, bool* is_directory) {
    int64_t slash_idx = fl_string_find_char(relative, '/');

    if (slash_idx >= 0) {
        *is_directory = true;
        return fl_string_substr(relative, 0, (uint64_t)slash_idx);
    }

    *is_directory = entry_is_directory;
    return relative;
}

// The child of dir that cached lies under. False when it is not below dir or names no child.

static inline bool vfs_archive_child_of(const VfsCachedEntry* cached, FlString dir, FlString* name,
                                        bool* is_directory) {
    FlString relative;
    if (!vfs_archive_relative_to(cached->path, dir, &relative)) {
        return false;
    }

    *name = vfs_archive_child_name(relative, cached->is_directory, is_directory);
    return name->length > 0;
}

typedef struct VfsSeenNames {
    FlString* names;
    uint64_t count;
} VfsSeenNames;

// Records a copy of name; false when it was already recorded.

static inline bool vfs_seen_names_add(VfsSeenNames* seen, FlArena* arena, FlString name) {
    for (uint64_t i = 0; i < seen->count; i++) {
        if (fl_string_equals(seen->names[i], name)) {
            return false;
        }
    }

    seen->names[seen->count++] = fl_string_copy(arena, name);
    return true;
}

// Calls callback once for each immediate child of path in a cached archive entry list, with shared
// directory prefixes collapsed to one entry. Passing "." or "" lists the archive root. Stops early
// if the callback returns false.

static inline void vfs_archive_list_children(VfsCachedEntry* entries, FlString path, FlVfsListingCallback callback,
                                             void* user_data) {
    if (fl_string_equals(path, FL_S("."))) {
        path = FL_S("");
    }

    arena_scratch_auto(temp);

    VfsSeenNames seen = { .names = arena_alloc_array(temp.arena, FlString, vfs_cached_entry_count(entries)) };

    for (VfsCachedEntry* cached = entries; cached; cached = cached->next) {
        FlString child_name;
        bool is_dir;

        if (!vfs_archive_child_of(cached, path, &child_name, &is_dir)
            || !vfs_seen_names_add(&seen, temp.arena, child_name)) {
            continue;
        }

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
