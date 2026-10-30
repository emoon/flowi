#include "archive_common.h"
#include <core/log.h>
#include <vfs/vfs_private.h>
#include <core/arena.h>
#include <core/heap.h>
#include <core/path.h>
#include <core/string.h>
#include "core/types.h"
#include <flowi/core/utils.h>
#include <flowi/vfs/vfs_plugin.h>
#include <flowi/vfs/vfs_types.h>
#include <lhasa.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory-backed input stream for lhasa

typedef struct LhasaMemoryStream {
    const uint8_t* data;
    size_t size;
    size_t pos;
} LhasaMemoryStream;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int lhasa_mem_read(void* handle, void* buf, size_t buf_len) {
    LhasaMemoryStream* stream = (LhasaMemoryStream*)handle;
    size_t remaining = stream->size - stream->pos;
    size_t to_read = buf_len < remaining ? buf_len : remaining;

    if (to_read > 0) {
        memcpy(buf, stream->data + stream->pos, to_read);
        stream->pos += to_read;
    }

    return (int)to_read;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int lhasa_mem_skip(void* handle, size_t bytes) {
    LhasaMemoryStream* stream = (LhasaMemoryStream*)handle;
    size_t remaining = stream->size - stream->pos;
    size_t to_skip = bytes < remaining ? bytes : remaining;
    stream->pos += to_skip;
    return (to_skip == bytes) ? 1 : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void lhasa_mem_close(void* handle) {
    (void)handle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static LHAInputStreamType s_lhasa_memory_stream_type = {
    .read = lhasa_mem_read,
    .skip = lhasa_mem_skip,
    .close = lhasa_mem_close,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle for the archive root or one of its members

typedef struct LhasaHandle {
    VfsArchiveHandle base;
    LhasaMemoryStream* mem_stream; // Memory stream state (for memory-based archives)
    LHAInputStream* stream;        // Lhasa input stream
    LHAReader* reader;             // Lhasa reader

    // Set once a read with no path has found the handle's own entry; later ones read on from the reader's
    // position in it.
    bool positioned;
} LhasaHandle;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// True for the .lha and .lzh extensions

static bool is_lha_file(FlString path) {
    return fl_path_has_extension(path, FL_S("lha")) || fl_path_has_extension(path, FL_S("lzh"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// True when the bytes start like an LHA archive

static bool is_lha_archive_data(const uint8_t* data, size_t size) {
    // Header layout: [header_size][checksum][-lh?-] or [-lz?-]
    if (size < 7) {
        return false;
    }

    if (data[2] == '-' && data[3] == 'l' && (data[4] == 'h' || data[4] == 'z') && data[6] == '-') {
        return true;
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Joins the header's path and filename, without a trailing slash

static size_t build_full_path(char* dest, size_t dest_size, LHAFileHeader* header) {
    const char* filename = header->filename ? header->filename : "";
    const char* path = header->path ? header->path : "";

    size_t path_len = strlen(path);
    size_t filename_len = strlen(filename);
    size_t full_len = path_len + filename_len;

    if (full_len >= dest_size || full_len == 0) {
        dest[0] = '\0';
        return 0;
    }

    if (path_len > 0) {
        memcpy(dest, path, path_len);
    }
    memcpy(dest + path_len, filename, filename_len);
    dest[full_len] = '\0';

    if (full_len > 0 && dest[full_len - 1] == '/') {
        dest[full_len - 1] = '\0';
        full_len--;
    }

    return full_len;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Creates the reader over the handle's memory buffer

static bool lhasa_open_from_memory(LhasaHandle* handle) {
    if (!handle->base.memory_buffer || handle->base.memory_size == 0) {
        return false;
    }

    LhasaMemoryStream* mem_stream = (LhasaMemoryStream*)fl_heap_alloc(sizeof(LhasaMemoryStream));
    if (!mem_stream) {
        return false;
    }

    mem_stream->data = handle->base.memory_buffer;
    mem_stream->size = (size_t)handle->base.memory_size;
    mem_stream->pos = 0;

    LHAInputStream* input = lha_input_stream_new(&s_lhasa_memory_stream_type, mem_stream);
    if (!input) {
        fl_heap_free(mem_stream);
        return false;
    }

    LHAReader* reader = lha_reader_new(input);
    if (!reader) {
        lha_input_stream_free(input);
        fl_heap_free(mem_stream);
        return false;
    }

    handle->mem_stream = mem_stream;
    handle->stream = input;
    handle->reader = reader;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frees the handle's reader and the streams under it

static void lhasa_free_reader(LhasaHandle* handle) {
    if (handle->reader) {
        lha_reader_free(handle->reader);
        handle->reader = nullptr;
    }
    if (handle->stream) {
        lha_input_stream_free(handle->stream);
        handle->stream = nullptr;
    }
    if (handle->mem_stream) {
        fl_heap_free(handle->mem_stream);
        handle->mem_stream = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rewinds the reader to the first entry

static bool lhasa_reset_reader(LhasaHandle* handle) {
    if (!handle->base.memory_buffer || handle->base.memory_size == 0) {
        return false;
    }

    lhasa_free_reader(handle);

    return lhasa_open_from_memory(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Finds the entry named target_path, leaving the reader positioned at it. nullptr when absent.

static LHAFileHeader* find_lha_entry(LhasaHandle* handle, FlString target_path) {
    if (!handle->base.memory_buffer || handle->base.memory_size == 0) {
        return nullptr;
    }

    if (!lhasa_reset_reader(handle)) {
        return nullptr;
    }

    arena_scratch_auto(temp);
    const char* target_cstr = fl_string_to_cstr(temp.arena, target_path);

    LHAFileHeader* header;
    while ((header = lha_reader_next_file(handle->reader)) != nullptr) {
        char full_path[1024];
        size_t full_len = build_full_path(full_path, sizeof(full_path), header);
        if (full_len == 0) {
            continue;
        }

        if (vfs_archive_entry_path_matches(full_path, target_cstr, target_path.length)) {
            return header;
        }
    }

    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void build_entry_cache(LhasaHandle* handle) {
    if (handle->base.entries.arena || !handle->base.memory_buffer) {
        return;
    }

    if (!lhasa_reset_reader(handle)) {
        return;
    }

    vfs_entry_cache_begin(&handle->base.entries);

    LHAFileHeader* header;
    while ((header = lha_reader_next_file(handle->reader)) != nullptr) {
        if (lha_reader_current_is_fake(handle->reader)) {
            continue;
        }

        char full_path[1024];
        size_t full_len = build_full_path(full_path, sizeof(full_path), header);
        if (full_len == 0) {
            continue;
        }

        vfs_entry_cache_add(&handle->base.entries, string_from_cstr(full_path), (int64_t)header->length,
                            strcmp(header->compress_method, LHA_COMPRESS_TYPE_DIR) == 0);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Opens a member of an already-opened archive

static FlVfsOpenResult lhasa_open(void* driver_instance, void* parent_handle, FlString path, uint32_t flags,
                                  FlVfsMountOptions mount_options) {
    (void)driver_instance;
    (void)flags;
    (void)mount_options;

    if (parent_handle) {
        LhasaHandle* parent = (LhasaHandle*)parent_handle;
        if (!find_lha_entry(parent, path)) {
            return (FlVfsOpenResult) { .handle = nullptr, .error = { .vfs_status = FlVfsStatus_FileNotFound } };
        }

        LhasaHandle* handle = (LhasaHandle*)fl_heap_alloc(sizeof(LhasaHandle));
        if (!handle) {
            return (FlVfsOpenResult) { .handle = nullptr, .error = { .vfs_status = FlVfsStatus_OutOfMemory } };
        }
        *handle = (LhasaHandle) { 0 };
        vfs_archive_handle_init_child(&handle->base, &parent->base, path);

        return (FlVfsOpenResult) { .handle = handle, .error = { .vfs_status = FlVfsStatus_Success } };
    }

    // An LHA archive opens only from memory, through lhasa_open_memory.
    if (!is_lha_file(path)) {
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_PathInvalid } };
    }

    logc_debug(VFS_ID, "Lhasa: Direct file open not supported, use memory-based open");
    return (FlVfsOpenResult) { .handle = nullptr,
                               .is_directory = false,
                               .error = { .vfs_status = FlVfsStatus_NotImplemented } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool lhasa_is_directory(void* driver_instance, void* handle) {
    (void)driver_instance;
    LhasaHandle* h = (LhasaHandle*)handle;
    return h ? h->base.is_directory : false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Continue reading the handle's own entry from where the last read stopped, 0 bytes at its end

static FlVfsReadResult lhasa_read_next(LhasaHandle* handle, uint8_t* buffer, int64_t size) {
    if (!handle->positioned) {
        if (!find_lha_entry(handle, handle->base.target_path)) {
            return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_FileNotFound } };
        }
        handle->positioned = true;
    }

    size_t total_read = 0;
    while (total_read < (size_t)size) {
        size_t chunk = lha_reader_read(handle->reader, buffer + total_read, (size_t)size - total_read);
        if (chunk == 0) {
            break;
        }
        total_read += chunk;
    }

    return (FlVfsReadResult) { .bytes_read = (int64_t)total_read, .error = { .vfs_status = FlVfsStatus_Success } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsReadResult lhasa_read(void* driver_instance, void* handle_ptr, FlString path, uint8_t* buffer,
                                  int64_t size) {
    (void)driver_instance;
    LhasaHandle* handle = (LhasaHandle*)handle_ptr;

    if (!handle) {
        logc_error(VFS_ID, "Invalid handle for read");
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
        return lhasa_read_next(handle, buffer, size);
    }

    // Finding the named entry moves the reader off wherever a continuation read left it.
    handle->positioned = false;
    LHAFileHeader* header = find_lha_entry(handle, target_path);
    if (!header) {
        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_FileNotFound } };
    }

    // A buffer smaller than the entry is refused.
    if ((uint64_t)size < header->length) {
        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_GenericError } };
    }

    size_t total_read = 0;
    size_t to_read = (size_t)size;

    while (total_read < to_read) {
        size_t chunk = lha_reader_read(handle->reader, buffer + total_read, to_read - total_read);
        if (chunk == 0) {
            break;
        }
        total_read += chunk;
    }

    return (FlVfsReadResult) { .bytes_read = (int64_t)total_read, .error = { .vfs_status = FlVfsStatus_Success } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsSizeResult lhasa_get_size(void* driver_instance, void* handle_ptr, FlString path) {
    (void)driver_instance;
    LhasaHandle* handle = (LhasaHandle*)handle_ptr;

    if (!handle) {
        logc_error(VFS_ID, "Invalid handle for get_size");
        return (FlVfsSizeResult) { .size = -1, .error = { .vfs_status = FlVfsStatus_InvalidHandle } };
    }

    FlString target_path = vfs_archive_resolve_target_path(&handle->base, path);
    if (target_path.length == 0) {
        logc_error(VFS_ID, "No target file specified for get_size");
        return (FlVfsSizeResult) { .size = -1, .error = { .vfs_status = FlVfsStatus_PathInvalid } };
    }

    // Searches on a reader of its own, leaving the handle's read position alone.
    LhasaHandle probe
        = { .base = { .memory_buffer = handle->base.memory_buffer, .memory_size = handle->base.memory_size } };
    LHAFileHeader* header = find_lha_entry(&probe, target_path);
    i64 size = header ? (i64)header->length : -1;
    lhasa_free_reader(&probe);

    if (size < 0) {
        return (FlVfsSizeResult) { .size = -1, .error = { .vfs_status = FlVfsStatus_FileNotFound } };
    }

    return (FlVfsSizeResult) { .size = size, .error = { .vfs_status = FlVfsStatus_Success } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void lhasa_list_directory(void* driver_instance, void* handle_ptr, FlString path, FlVfsListingCallback callback,
                                 void* user_data) {
    (void)driver_instance;
    LhasaHandle* handle = (LhasaHandle*)handle_ptr;

    if (!handle || !handle->base.is_directory) {
        return;
    }

    build_entry_cache(handle);

    vfs_archive_list_children(handle->base.entries.head, path, callback, user_data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void lhasa_close(void* driver_instance, void* handle_ptr) {
    (void)driver_instance;
    LhasaHandle* handle = (LhasaHandle*)handle_ptr;

    if (!handle) {
        return;
    }

    lhasa_free_reader(handle);
    vfs_archive_handle_release(&handle->base);
    fl_heap_free(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsOpenResult lhasa_open_memory(void* driver_instance, const uint8_t* data, uint64_t size,
                                         FlString original_path) {
    (void)driver_instance;

    if (!data || size == 0) {
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_PathInvalid } };
    }

    if (!is_lha_archive_data(data, (size_t)size)) {
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_ArchiveError } };
    }

    LhasaHandle* handle = (LhasaHandle*)fl_heap_alloc(sizeof(LhasaHandle));
    if (!handle) {
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_OutOfMemory } };
    }

    *handle = (LhasaHandle) { 0 };
    vfs_archive_handle_init_root(&handle->base, data, size);

    if (!lhasa_open_from_memory(handle)) {
        logc_debug(VFS_ID, "Failed to open LHA archive from memory (size: %llu)", (unsigned long long)size);
        lhasa_close(driver_instance, handle);
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_ArchiveError } };
    }

    handle->base.owns_memory = true;

    logc_debug(VFS_ID, "Opened LHA archive with lhasa (size: %llu)", (unsigned long long)size);

    return (FlVfsOpenResult) { .handle = handle, .is_directory = true, .error = { .vfs_status = FlVfsStatus_Success } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsDriverCapabilities lhasa_get_capabilities(void) {
    return FlVfsDriverCapabilities_Read | FlVfsDriverCapabilities_List | FlVfsDriverCapabilities_Memory
           | FlVfsDriverCapabilities_Archive;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsPlugin s_lhasa_plugin = {
    .api_version = FL_VFS_PLUGIN_API_VERSION,
    .plugin_name = FL_S_("Lhasa"),
    .plugin_info = nullptr,

    .create = nullptr,
    .destroy = nullptr,
    .supports_path = is_lha_file,
    .can_open = is_lha_file,
    .open = lhasa_open,
    .is_directory = lhasa_is_directory,
    .read = lhasa_read,
    .get_size = lhasa_get_size,
    .list_directory = lhasa_list_directory,
    .close = lhasa_close,
    .get_capabilities = lhasa_get_capabilities,

    .open_memory = lhasa_open_memory,
    .get_file_path = nullptr,
    .is_cached = nullptr,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVfsPlugin* vfs_driver_lhasa(void) {
    return &s_lhasa_plugin;
}
