#include "archive_common.h"
#include <core/log.h>
#include <vfs/vfs_private.h>
#include <archive.h>
#include <archive_entry.h>
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

#define LIBARCHIVE_BLOCK_SIZE 10240

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle for the archive root or one of its members

typedef struct LibArchiveHandle {
    VfsArchiveHandle base;
    FlString archive_path;       // Path to the archive file itself
    struct archive* archive;     // libarchive handle
    bool is_raw_compression;     // True if this is single-file compression (.gz, .bz2, etc.)
    FlString decompressed_name;  // Filename after stripping compression extension (for raw format)
    bool owns_decompressed_name; // Whether this handle owns decompressed_name (must free in close)
    int64_t entry_size;          // Size of the entry the archive is positioned at (-1 if unknown)
    bool entry_ready;            // True if archive is positioned at entry for reading
} LibArchiveHandle;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Maps a libarchive return code to a VFS status

static FlVfsStatus libarchive_errno_to_vfs_status(int archive_result) {
    switch (archive_result) {
        case ARCHIVE_EOF:
            return FlVfsStatus_FileNotFound;
        case ARCHIVE_RETRY:
        case ARCHIVE_WARN:
            return FlVfsStatus_GenericError;
        case ARCHIVE_FAILED:
            return FlVfsStatus_ReadError;
        case ARCHIVE_FATAL:
            return FlVfsStatus_ArchiveError;
        default:
            return FlVfsStatus_GenericError;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// True when the path ends in ".<suffix>", compared case-insensitively. Suffix may contain dots ("tar.gz")

static bool path_has_suffix(FlString path, FlString suffix) {
    if (path.length <= suffix.length + 1 || path.data[path.length - suffix.length - 1] != '.') {
        return false;
    }
    return fl_string_equals_nocase(fl_string_substr(path, path.length - suffix.length, suffix.length), suffix);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool path_has_any_suffix(FlString path, const FlString* suffixes, size_t count) {
    for_count(i, count) {
        if (path_has_suffix(path, suffixes[i])) {
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compressed tarballs and single-file compression formats

static const FlString k_tarball_suffixes[] = { FL_S_("tar.gz"),   FL_S_("tar.xz"), FL_S_("tar.lz"), FL_S_("tar.bz2"),
                                               FL_S_("tar.lzma"), FL_S_("tgz"),    FL_S_("tbz2"),   FL_S_("txz") };

static const FlString k_single_file_suffixes[]
    = { FL_S_("gz"), FL_S_("bz2"), FL_S_("xz"), FL_S_("lz"), FL_S_("lzma"), FL_S_("zst") };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// True for single-file compression (.gz, .bz2, ...) but not a compressed tarball (.tar.gz); these open with
// archive_read_support_format_raw()

static bool is_single_file_compression(FlString path) {
    if (path_has_any_suffix(path, k_tarball_suffixes, sizeof(k_tarball_suffixes) / sizeof(k_tarball_suffixes[0]))) {
        return false;
    }
    return path_has_any_suffix(path, k_single_file_suffixes,
                               sizeof(k_single_file_suffixes) / sizeof(k_single_file_suffixes[0]));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The filename without its compression extension ("MOD.song.gz" -> "MOD.song")

static FlString get_decompressed_filename(FlString path, FlArena* arena) {
    FlString filename = fl_path_get_filename(path);
    if (filename.length == 0) {
        return filename;
    }

    FlString comp_exts[] = { FL_S(".gz"), FL_S(".bz2"), FL_S(".xz"), FL_S(".lz"), FL_S(".lzma"), FL_S(".zst") };
    for_count(i, sizeof(comp_exts) / sizeof(comp_exts[0])) {
        FlString ext = comp_exts[i];
        if (filename.length > ext.length) {
            FlString suffix = fl_string_substr(filename, filename.length - ext.length, ext.length);
            if (fl_string_equals_nocase(suffix, ext)) {
                return fl_string_copy(arena, fl_string_substr(filename, 0, filename.length - ext.length));
            }
        }
    }
    return fl_string_copy(arena, filename);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_archive_file(FlString path) {
    if (is_single_file_compression(path)) {
        return true;
    }

    static const FlString k_archive_suffixes[]
        = { FL_S_("zip"), FL_S_("tar"), FL_S_("7z"), FL_S_("rar"), FL_S_("cab"), FL_S_("iso"), FL_S_("cpio") };

    return path_has_any_suffix(path, k_archive_suffixes, sizeof(k_archive_suffixes) / sizeof(k_archive_suffixes[0]))
           || path_has_any_suffix(path, k_tarball_suffixes, sizeof(k_tarball_suffixes) / sizeof(k_tarball_suffixes[0]));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enables the formats and filters the handle's archive is read with

static void configure_archive_format(struct archive* a, bool is_raw_compression) {
    if (is_raw_compression) {
        archive_read_support_format_raw(a);
    } else {
        archive_read_support_format_all(a);
    }
    archive_read_support_filter_all(a);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Opens a fresh reader over the handle's memory or file

static struct archive* open_archive_reader(LibArchiveHandle* handle, FlArena* scratch) {
    struct archive* a = archive_read_new();
    configure_archive_format(a, handle->is_raw_compression);

    int r;
    if (handle->base.memory_buffer) {
        r = archive_read_open_memory(a, handle->base.memory_buffer, handle->base.memory_size);
    } else {
        const char* archive_path_cstr = fl_string_to_cstr(scratch, handle->archive_path);
        r = archive_read_open_filename(a, archive_path_cstr, LIBARCHIVE_BLOCK_SIZE);
    }

    if (r != ARCHIVE_OK) {
        logc_error(VFS_ID, "Failed to open archive: %s", archive_error_string(a));
        archive_read_free(a);
        return nullptr;
    }

    return a;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Advances the archive to the entry named target_cstr and stores it in entry_out when given. False when absent.

static bool find_archive_entry(struct archive* a, const char* target_cstr, size_t target_len,
                               struct archive_entry** entry_out) {
    struct archive_entry* entry;

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* entry_path = archive_entry_pathname(entry);
        if (!entry_path) {
            archive_read_data_skip(a);
            continue;
        }

        if (vfs_archive_entry_path_matches(entry_path, target_cstr, target_len)) {
            if (entry_out) {
                *entry_out = entry;
            }
            return true;
        }

        archive_read_data_skip(a);
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void libarchive_close(void* driver_instance, void* handle_ptr);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Positions the handle's archive at its target entry. Success when found, an error status otherwise.

static FlVfsStatus libarchive_seek_to_entry(LibArchiveHandle* handle, FlArena* scratch) {
    if (handle->entry_ready) {
        return FlVfsStatus_Success;
    }

    if (handle->base.target_path.length == 0) {
        return FlVfsStatus_PathInvalid;
    }

    if (!handle->archive) {
        handle->archive = open_archive_reader(handle, scratch);
        if (!handle->archive) {
            return FlVfsStatus_ArchiveError;
        }
    }

    struct archive_entry* entry;

    // A raw compression stream has the one entry
    if (handle->is_raw_compression) {
        if (archive_read_next_header(handle->archive, &entry) == ARCHIVE_OK) {
            handle->entry_size = archive_entry_size(entry); // May be 0 (unknown) for raw format
            handle->entry_ready = true;
            return FlVfsStatus_Success;
        }
        return FlVfsStatus_FileNotFound;
    }

    const char* target_cstr = fl_string_to_cstr(scratch, handle->base.target_path);

    if (find_archive_entry(handle->archive, target_cstr, handle->base.target_path.length, &entry)) {
        handle->entry_size = archive_entry_size(entry);
        handle->entry_ready = true;
        return FlVfsStatus_Success;
    }

    return FlVfsStatus_FileNotFound;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Allocates the handle of an archive root. data is the archive's bytes, or nullptr for a file-backed one.

static LibArchiveHandle* libarchive_alloc_root(FlString archive_path, const uint8_t* data, uint64_t size,
                                               FlArena* scratch) {
    LibArchiveHandle* handle = (LibArchiveHandle*)fl_heap_alloc(sizeof(LibArchiveHandle));
    if (!handle) {
        return nullptr;
    }
    *handle = (LibArchiveHandle) { 0 };
    vfs_archive_handle_init_root(&handle->base, data, size);

    handle->archive_path = fl_string_copy_malloc(archive_path);
    handle->is_raw_compression = is_single_file_compression(archive_path);
    handle->decompressed_name
        = handle->is_raw_compression ? get_decompressed_filename(archive_path, scratch) : FL_S("");
    if (handle->is_raw_compression && handle->decompressed_name.length > 0) {
        handle->decompressed_name = fl_string_copy_malloc(handle->decompressed_name);
        handle->owns_decompressed_name = true;
    }
    return handle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Opens a member of an already-opened archive, or the archive file itself

static FlVfsOpenResult libarchive_open(void* driver_instance, void* parent_handle, FlString path, uint32_t flags,
                                       FlVfsMountOptions mount_options) {
    (void)flags;
    (void)mount_options;

    if (parent_handle) {
        LibArchiveHandle* parent = (LibArchiveHandle*)parent_handle;
        LibArchiveHandle* handle = (LibArchiveHandle*)fl_heap_alloc(sizeof(LibArchiveHandle));
        if (!handle) {
            return (FlVfsOpenResult) { .handle = nullptr, .error = { .vfs_status = FlVfsStatus_OutOfMemory } };
        }
        *handle = (LibArchiveHandle) { 0 };
        vfs_archive_handle_init_child(&handle->base, &parent->base, path);

        handle->archive_path = fl_string_copy_malloc(parent->archive_path);
        handle->is_raw_compression = parent->is_raw_compression;
        handle->decompressed_name = parent->decompressed_name; // Shared with the parent, not owned
        handle->entry_size = -1;

        arena_scratch_auto(temp);
        FlVfsStatus status = libarchive_seek_to_entry(handle, temp.arena);
        if (status != FlVfsStatus_Success) {
            libarchive_close(driver_instance, handle);
            return (FlVfsOpenResult) { .handle = nullptr, .error = { .vfs_status = status } };
        }

        return (FlVfsOpenResult) { .handle = handle, .error = { .vfs_status = FlVfsStatus_Success } };
    }

    if (!is_archive_file(path)) {
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_PathInvalid } };
    }

    arena_scratch_auto(temp);

    LibArchiveHandle* handle = libarchive_alloc_root(path, nullptr, 0, temp.arena);
    if (!handle) {
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_OutOfMemory } };
    }

    handle->archive = archive_read_new();
    configure_archive_format(handle->archive, handle->is_raw_compression);

    const char* archive_path_cstr = fl_string_to_cstr(temp.arena, path);

    int r = archive_read_open_filename(handle->archive, archive_path_cstr, LIBARCHIVE_BLOCK_SIZE);
    if (r != ARCHIVE_OK) {
        FlVfsStatus status = libarchive_errno_to_vfs_status(r);
        int native_error = archive_errno(handle->archive);
        libarchive_close(driver_instance, handle);

        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = status, .native_error = native_error } };
    }

    struct archive_entry* entry;
    r = archive_read_next_header(handle->archive, &entry);
    if (r != ARCHIVE_OK && r != ARCHIVE_EOF) {
        logc_debug(VFS_ID, "Archive validation failed for: %.*s", (int)path.length, path.data);
        libarchive_close(driver_instance, handle);

        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_ArchiveError } };
    }

    archive_read_free(handle->archive);
    handle->archive = archive_read_new();
    configure_archive_format(handle->archive, handle->is_raw_compression);

    r = archive_read_open_filename(handle->archive, archive_path_cstr, LIBARCHIVE_BLOCK_SIZE);
    if (r != ARCHIVE_OK) {
        FlVfsStatus status = libarchive_errno_to_vfs_status(r);
        libarchive_close(driver_instance, handle);

        return (FlVfsOpenResult) { .handle = nullptr, .is_directory = false, .error = { .vfs_status = status } };
    }

    return (FlVfsOpenResult) { .handle = handle, .is_directory = true, .error = { .vfs_status = FlVfsStatus_Success } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool libarchive_is_directory(void* driver_instance, void* handle) {
    (void)driver_instance;
    LibArchiveHandle* h = (LibArchiveHandle*)handle;
    return h ? h->base.is_directory : false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsReadResult libarchive_read(void* driver_instance, void* handle_ptr, FlString path, uint8_t* buffer,
                                       int64_t size) {
    (void)driver_instance;
    LibArchiveHandle* handle = (LibArchiveHandle*)handle_ptr;

    if (!handle) {
        logc_error(VFS_ID, "Invalid handle for read");
        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_InvalidHandle } };
    }

    FlString target_path = vfs_archive_resolve_target_path(&handle->base, path);
    if (target_path.length == 0) {
        logc_error(VFS_ID, "No target file specified for read");
        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_PathInvalid } };
    }

    if (handle->entry_ready && handle->archive) {
        la_ssize_t bytes_read = archive_read_data(handle->archive, buffer, (size_t)size);
        if (bytes_read < 0) {
            logc_error(VFS_ID, "Failed to read from archive entry: %S (libarchive: %s)", handle->base.target_path,
                       archive_error_string(handle->archive));
            return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_ReadError } };
        }
        return (FlVfsReadResult) { .bytes_read = (int64_t)bytes_read, .error = { .vfs_status = FlVfsStatus_Success } };
    }

    arena_scratch_auto(temp);

    struct archive* a = open_archive_reader(handle, temp.arena);
    if (!a) {
        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_ArchiveError } };
    }

    struct archive_entry* entry = nullptr;
    if (handle->is_raw_compression) {
        if (archive_read_next_header(a, &entry) != ARCHIVE_OK) {
            archive_read_free(a);
            return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_FileNotFound } };
        }
    } else {
        const char* target_cstr = fl_string_to_cstr(temp.arena, target_path);
        if (!find_archive_entry(a, target_cstr, target_path.length, nullptr)) {
            archive_read_free(a);
            return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_FileNotFound } };
        }
    }

    la_ssize_t bytes_read = archive_read_data(a, buffer, (size_t)size);

    if (bytes_read < 0) {
        logc_error(VFS_ID, "Failed to read from archive entry: %.*s", (int)target_path.length, target_path.data);
        archive_read_free(a);

        return (FlVfsReadResult) { .bytes_read = -1, .error = { .vfs_status = FlVfsStatus_ReadError } };
    }

    archive_read_free(a);

    return (FlVfsReadResult) { .bytes_read = (int64_t)bytes_read, .error = { .vfs_status = FlVfsStatus_Success } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsSizeResult libarchive_get_size(void* driver_instance, void* handle_ptr, FlString path) {
    (void)driver_instance;
    LibArchiveHandle* handle = (LibArchiveHandle*)handle_ptr;

    if (!handle) {
        logc_error(VFS_ID, "Invalid handle for get_size");
        return (FlVfsSizeResult) { .size = -1, .error = { .vfs_status = FlVfsStatus_InvalidHandle } };
    }

    if (handle->entry_ready && handle->entry_size >= 0) {
        return (FlVfsSizeResult) { .size = handle->entry_size, .error = { .vfs_status = FlVfsStatus_Success } };
    }

    FlString target_path = vfs_archive_resolve_target_path(&handle->base, path);
    if (target_path.length == 0) {
        logc_error(VFS_ID, "No target file specified for get_size");
        return (FlVfsSizeResult) { .size = -1, .error = { .vfs_status = FlVfsStatus_PathInvalid } };
    }

    arena_scratch_auto(temp);

    struct archive* a = open_archive_reader(handle, temp.arena);
    if (!a) {
        return (FlVfsSizeResult) { .size = -1, .error = { .vfs_status = FlVfsStatus_ArchiveError } };
    }

    struct archive_entry* entry = nullptr;

    if (handle->is_raw_compression) {
        if (archive_read_next_header(a, &entry) != ARCHIVE_OK) {
            archive_read_free(a);
            return (FlVfsSizeResult) { .size = -1, .error = { .vfs_status = FlVfsStatus_FileNotFound } };
        }
    } else {
        const char* target_cstr = fl_string_to_cstr(temp.arena, target_path);
        if (!find_archive_entry(a, target_cstr, target_path.length, &entry)) {
            archive_read_free(a);
            return (FlVfsSizeResult) { .size = -1, .error = { .vfs_status = FlVfsStatus_FileNotFound } };
        }
    }

    int64_t result_size = archive_entry_size(entry);
    archive_read_free(a);

    return (FlVfsSizeResult) { .size = result_size, .error = { .vfs_status = FlVfsStatus_Success } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reads every entry header into the listing cache

static void build_entry_cache(LibArchiveHandle* handle) {
    if (!vfs_entry_cache_begin(&handle->base.entries)) {
        return;
    }

    struct archive_entry* entry;
    while (archive_read_next_header(handle->archive, &entry) == ARCHIVE_OK) {
        FlString path;

        // Raw compression lists the decompressed filename in place of libarchive's "data"
        if (handle->is_raw_compression && handle->decompressed_name.length > 0) {
            path = handle->decompressed_name;
        } else {
            const char* entry_name = archive_entry_pathname(entry);
            if (!entry_name) {
                archive_read_data_skip(handle->archive);
                continue;
            }
            path = string_from_cstr(entry_name);
        }

        if (path.length > 0 && path.data[path.length - 1] == '/') {
            path.length--;
        }

        if (path.length == 0) {
            archive_read_data_skip(handle->archive);
            continue;
        }

        vfs_entry_cache_add(&handle->base.entries, path, archive_entry_size(entry),
                            archive_entry_filetype(entry) == AE_IFDIR);
        archive_read_data_skip(handle->archive);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void libarchive_list_directory(void* driver_instance, void* handle_ptr, FlString path,
                                      FlVfsListingCallback callback, void* user_data) {
    (void)driver_instance;
    LibArchiveHandle* handle = (LibArchiveHandle*)handle_ptr;

    if (!handle || !handle->base.is_directory) {
        return;
    }

    build_entry_cache(handle);

    vfs_archive_list_children(handle->base.entries.head, path, callback, user_data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void libarchive_close(void* driver_instance, void* handle_ptr) {
    (void)driver_instance;
    LibArchiveHandle* handle = (LibArchiveHandle*)handle_ptr;

    if (!handle) {
        return;
    }

    if (handle->archive) {
        archive_read_free(handle->archive);
        handle->archive = nullptr;
    }

    fl_string_free(handle->archive_path);
    if (handle->owns_decompressed_name) {
        fl_string_free(handle->decompressed_name);
    }
    vfs_archive_handle_release(&handle->base);
    fl_heap_free(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsOpenResult libarchive_open_memory(void* driver_instance, const uint8_t* data, uint64_t size,
                                              FlString original_path) {
    (void)driver_instance;

    if (!data || size == 0) {
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_PathInvalid } };
    }

    arena_scratch_auto(temp);

    LibArchiveHandle* handle = libarchive_alloc_root(original_path, data, size, temp.arena);
    if (!handle) {
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_OutOfMemory } };
    }

    handle->archive = archive_read_new();
    configure_archive_format(handle->archive, handle->is_raw_compression);

    int r = archive_read_open_memory(handle->archive, (void*)data, (size_t)size);
    if (r != ARCHIVE_OK) {
        logc_debug(VFS_ID, "Failed to open archive from memory buffer (size: %llu) - %s", (unsigned long long)size,
                   archive_error_string(handle->archive));
        libarchive_close(driver_instance, handle);
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_ArchiveError } };
    }

    struct archive_entry* entry = nullptr;
    r = archive_read_next_header(handle->archive, &entry);
    if (r != ARCHIVE_OK && r != ARCHIVE_EOF) {
        logc_debug(VFS_ID, "Failed to validate archive from memory buffer (size: %llu)", (unsigned long long)size);
        libarchive_close(driver_instance, handle);
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = FlVfsStatus_ArchiveError } };
    }

    archive_read_free(handle->archive);
    handle->archive = archive_read_new();
    configure_archive_format(handle->archive, handle->is_raw_compression);

    r = archive_read_open_memory(handle->archive, (void*)data, (size_t)size);
    if (r != ARCHIVE_OK) {
        libarchive_close(driver_instance, handle);
        return (FlVfsOpenResult) { .handle = nullptr,
                                   .is_directory = false,
                                   .error = { .vfs_status = libarchive_errno_to_vfs_status(r) } };
    }

    handle->base.owns_memory = true;

    return (FlVfsOpenResult) { .handle = handle, .is_directory = true, .error = { .vfs_status = FlVfsStatus_Success } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsDriverCapabilities libarchive_get_capabilities(void) {
    return FlVfsDriverCapabilities_Read | FlVfsDriverCapabilities_List | FlVfsDriverCapabilities_Memory
           | FlVfsDriverCapabilities_Archive;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsPlugin s_libarchive_plugin = {
    .api_version = FL_VFS_PLUGIN_API_VERSION,
    .plugin_name = FL_S_("LibArchive"),
    .plugin_info = nullptr,

    .create = nullptr,
    .destroy = nullptr,
    .supports_path = is_archive_file,
    .can_open = is_archive_file,
    .open = libarchive_open,
    .is_directory = libarchive_is_directory,
    .read = libarchive_read,
    .get_size = libarchive_get_size,
    .list_directory = libarchive_list_directory,
    .close = libarchive_close,
    .get_capabilities = libarchive_get_capabilities,

    .open_memory = libarchive_open_memory,
    .get_file_path = nullptr,
    .is_cached = nullptr,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVfsPlugin* vfs_driver_libarchive(void) {
    return &s_libarchive_plugin;
}
