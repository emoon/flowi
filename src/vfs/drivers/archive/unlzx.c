#include "archive_common.h"
#include <core/log.h>
#include <vfs/vfs_private.h>
#include <core/arena.h>
#include <core/heap.h>
#include <core/path.h>
#include <core/string.h>
#include <flowi/core/platform.h>
#include "core/types.h"
#include <flowi/core/utils.h>
#include <flowi/vfs/vfs_plugin.h>
#include <flowi/vfs/vfs_types.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unlzx.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle for the archive root or one of its members

typedef struct UnlzxHandle {
    VfsArchiveHandle base;
    bool owns_archive;     // This handle closes archive with itself
    UnlzxArchive* archive; // UnLZX archive handle

    // A read with no path continues through the handle's own entry; the stream opens on the first such read and
    // decompresses straight into each caller's buffer.
    UnlzxStream* stream;
} UnlzxHandle;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// True for the .lzx extension

static bool is_lzx_file(FlString path) {
    return fl_path_has_extension(path, FL_S("lzx"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// True when the bytes start like an LZX archive

static bool is_lzx_archive_data(const uint8_t* data, size_t size) {
    if (size < 10) {
        return false;
    }
    return (data[0] == 'L' && data[1] == 'Z' && data[2] == 'X');
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void build_entry_cache(UnlzxHandle* handle) {
    if (handle->base.entries.arena || !handle->archive) {
        return;
    }

    vfs_entry_cache_begin(&handle->base.entries);

    int entry_count = unlzx_get_entry_count(handle->archive);
    for (int i = 0; i < entry_count; i++) {
        const UnlzxEntry* entry = unlzx_get_entry(handle->archive, i);
        if (!entry || !entry->filename) {
            continue;
        }

        vfs_entry_cache_add(&handle->base.entries, string_from_cstr(entry->filename), (int64_t)entry->unpack_size,
                            false);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Opens a member of an already-opened archive

static FlVfsOpenResult unlzx_open(void* driver_instance, void* parent_handle, FlString path, uint32_t flags,
                                  FlVfsMountOptions mount_options) {
    (void)driver_instance;
    (void)flags;
    (void)mount_options;

    if (parent_handle) {
        UnlzxHandle* parent = (UnlzxHandle*)parent_handle;
        {
            arena_scratch_auto(temp);
            if (!unlzx_find_entry(parent->archive, fl_string_to_cstr(temp.arena, path))) {
                return (FlVfsOpenResult) { .handle = nullptr, .error = { .vfs_status = FlVfsStatus_FileNotFound } };
            }
        }

        UnlzxHandle* handle = (UnlzxHandle*)fl_heap_alloc(sizeof(UnlzxHandle));
        if (!handle) {
            return (FlVfsOpenResult) { .handle = nullptr, .error = { .vfs_status = FlVfsStatus_OutOfMemory } };
        }
        *handle = (UnlzxHandle) { 0 };
        vfs_archive_handle_init_child(&handle->base, &parent->base, path);
        handle->archive = parent->archive;

        return (FlVfsOpenResult) { .handle = handle, .error = { .vfs_status = FlVfsStatus_Success } };
    }

    // An LZX archive opens only from memory, through unlzx_vfs_open_memory.
    if (!is_lzx_file(path)) {
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_PathInvalid } };
    }

    logc_debug(VFS_ID, "UnLZX: Direct file open not supported, use memory-based open");
    return (FlVfsOpenResult) { .handle = nullptr,
                               .is_directory = false,
                               .error = { .vfs_status = FlVfsStatus_NotImplemented } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool unlzx_is_directory(void* driver_instance, void* handle) {
    (void)driver_instance;
    UnlzxHandle* h = (UnlzxHandle*)handle;
    return h ? h->base.is_directory : false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Continue reading the handle's own entry from where the last read stopped, 0 bytes at its end

static FlVfsReadResult unlzx_read_next(UnlzxHandle* handle, uint8_t* buffer, int64_t size) {
    if (!handle->stream) {
        arena_scratch_auto(temp);
        const UnlzxEntry* entry
            = unlzx_find_entry(handle->archive, fl_string_to_cstr(temp.arena, handle->base.target_path));
        if (!entry) {
            return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_FileNotFound } };
        }

        UnlzxError err = unlzx_stream_open(handle->archive, entry, &handle->stream);
        if (err != UNLZX_OK) {
            logc_error(VFS_ID, "UnLZX extraction failed: %d", err);
            FlVfsStatus status = err == UNLZX_ERR_OUT_OF_MEMORY ? FlVfsStatus_OutOfMemory : FlVfsStatus_ArchiveError;
            return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = status } };
        }
    }

    size_t bytes_read = 0;
    UnlzxError err = unlzx_stream_read(handle->stream, buffer, (size_t)size, &bytes_read);
    if (err != UNLZX_OK) {
        logc_error(VFS_ID, "UnLZX extraction failed: %d", err);
        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_ArchiveError } };
    }

    return (FlVfsReadResult) { .bytes_read = (int64_t)bytes_read, .error = { .vfs_status = FlVfsStatus_Success } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsReadResult unlzx_read(void* driver_instance, void* handle_ptr, FlString path, uint8_t* buffer,
                                  int64_t size) {
    (void)driver_instance;
    UnlzxHandle* handle = (UnlzxHandle*)handle_ptr;

    if (!handle) {
        logc_error(VFS_ID, "Invalid handle for read");
        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_InvalidHandle } };
    }

    if (!handle->archive) {
        logc_error(VFS_ID, "No archive opened");
        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_InvalidHandle } };
    }

    FlString target_path = vfs_archive_resolve_target_path(&handle->base, path);
    if (target_path.length == 0) {
        logc_error(VFS_ID, "No target file specified for read");
        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_PathInvalid } };
    }

    if (size < 0) {
        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_GenericError } };
    }

    if (path.length == 0) {
        return unlzx_read_next(handle, buffer, size);
    }

    arena_scratch_auto(temp);
    const char* target_cstr = fl_string_to_cstr(temp.arena, target_path);

    const UnlzxEntry* entry = unlzx_find_entry(handle->archive, target_cstr);
    if (!entry) {
        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_FileNotFound } };
    }

    if ((uint64_t)size < entry->unpack_size) {
        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_GenericError } };
    }

    UnlzxError err = unlzx_extract_entry(handle->archive, entry, buffer, (size_t)size);
    if (err != UNLZX_OK) {
        logc_error(VFS_ID, "UnLZX extraction failed: %d", err);
        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_ArchiveError } };
    }

    return (FlVfsReadResult) { .bytes_read = (int64_t)entry->unpack_size,
                               .error = { .vfs_status = FlVfsStatus_Success } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsSizeResult unlzx_get_size(void* driver_instance, void* handle_ptr, FlString path) {
    (void)driver_instance;
    UnlzxHandle* handle = (UnlzxHandle*)handle_ptr;

    if (!handle) {
        logc_error(VFS_ID, "Invalid handle for get_size");
        return (FlVfsSizeResult) { .size = -1, .error = { .vfs_status = FlVfsStatus_InvalidHandle } };
    }

    if (!handle->archive) {
        logc_error(VFS_ID, "No archive opened");
        return (FlVfsSizeResult) { .size = -1, .error = { .vfs_status = FlVfsStatus_InvalidHandle } };
    }

    FlString target_path = vfs_archive_resolve_target_path(&handle->base, path);
    if (target_path.length == 0) {
        logc_error(VFS_ID, "No target file specified for get_size");
        return (FlVfsSizeResult) { .size = -1, .error = { .vfs_status = FlVfsStatus_PathInvalid } };
    }

    arena_scratch_auto(temp);
    const char* target_cstr = fl_string_to_cstr(temp.arena, target_path);

    const UnlzxEntry* entry = unlzx_find_entry(handle->archive, target_cstr);
    if (!entry) {
        return (FlVfsSizeResult) { .size = -1, .error = { .vfs_status = FlVfsStatus_FileNotFound } };
    }

    return (FlVfsSizeResult) { .size = (int64_t)entry->unpack_size, .error = { .vfs_status = FlVfsStatus_Success } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void unlzx_list_directory(void* driver_instance, void* handle_ptr, FlString path, FlVfsListingCallback callback,
                                 void* user_data) {
    (void)driver_instance;
    UnlzxHandle* handle = (UnlzxHandle*)handle_ptr;

    if (!handle || !handle->base.is_directory) {
        return;
    }

    build_entry_cache(handle);

    vfs_archive_list_children(handle->base.entries.head, path, callback, user_data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void unlzx_vfs_close(void* driver_instance, void* handle_ptr) {
    (void)driver_instance;
    UnlzxHandle* handle = (UnlzxHandle*)handle_ptr;

    if (!handle) {
        return;
    }

    if (handle->owns_archive && handle->archive) {
        unlzx_close(handle->archive);
        handle->archive = nullptr;
    }

    unlzx_stream_close(handle->stream);
    vfs_archive_handle_release(&handle->base);
    fl_heap_free(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsOpenResult unlzx_vfs_open_memory(void* driver_instance, const uint8_t* data, uint64_t size,
                                             FlString original_path) {
    (void)driver_instance;

    if (!data || size == 0) {
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_PathInvalid } };
    }

    if (!is_lzx_archive_data(data, (size_t)size)) {
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_ArchiveError } };
    }

    UnlzxHandle* handle = (UnlzxHandle*)fl_heap_alloc(sizeof(UnlzxHandle));
    if (!handle) {
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_OutOfMemory } };
    }

    *handle = (UnlzxHandle) { 0 };
    vfs_archive_handle_init_root(&handle->base, data, size);
    handle->owns_archive = true;

    handle->archive = unlzx_open_memory(data, (size_t)size);
    if (!handle->archive) {
        logc_debug(VFS_ID, "Failed to open LZX archive from memory (size: %llu)", (unsigned long long)size);
        unlzx_vfs_close(driver_instance, handle);
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_ArchiveError } };
    }

    handle->base.owns_memory = true;

    logc_debug(VFS_ID, "Opened LZX archive with unlzx (size: %llu, entries: %d)", (unsigned long long)size,
               unlzx_get_entry_count(handle->archive));

    return (FlVfsOpenResult) { .handle = handle, .is_directory = true, .error = { .vfs_status = FlVfsStatus_Success } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsDriverCapabilities unlzx_get_capabilities(void) {
    return FlVfsDriverCapabilities_Read | FlVfsDriverCapabilities_List | FlVfsDriverCapabilities_Memory
           | FlVfsDriverCapabilities_Archive;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsPlugin s_unlzx_plugin = {
    .api_version = FL_VFS_PLUGIN_API_VERSION,
    .plugin_name = FL_S_("UnLZX"),
    .plugin_info = nullptr,

    .create = nullptr,
    .destroy = nullptr,
    .supports_path = is_lzx_file,
    .can_open = is_lzx_file,
    .open = unlzx_open,
    .is_directory = unlzx_is_directory,
    .read = unlzx_read,
    .get_size = unlzx_get_size,
    .list_directory = unlzx_list_directory,
    .close = unlzx_vfs_close,
    .get_capabilities = unlzx_get_capabilities,

    .open_memory = unlzx_vfs_open_memory,
    .get_file_path = nullptr,
    .is_cached = nullptr,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVfsPlugin* vfs_driver_unlzx(void) {
    return &s_unlzx_plugin;
}
