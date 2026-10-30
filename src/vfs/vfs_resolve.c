// VFS Path Resolution
// Resolves a mount's source path to a driver and one root handle, descending through nested archives

#include <core/log.h>
#include <core/memory.h>
#include <core/path.h>
#include <core/profile.h>
#include <core/string.h>
#include "vfs_private.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Closes a plugin handle; every call site passes a live one.

static void close_plugin_handle(const FlVfsPlugin* plugin, void* instance, void* handle) {
    FL_VALIDATE(handle != nullptr);
    plugin->close(instance, handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Plugin utilities

FlString vfs_path_range(const PathComponents* components, int start, u32 end, FlArena* arena) {
    PathComponents range = *components;
    if (start > 0) {
        range.is_absolute = false;
    }
    return path_reconstruct_from_segments(&range, start, end, arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    DriverHandle opened;
    int error;
} MemoryArchive;

static TryLoadData try_open_file(VfsPluginEntry* plugin_entry, void* parent_handle, const int range_start,
                                 const int range_end, const PathComponents* components, FlArena* scratch) {

    if (!plugin_entry || components->segments.count == 0) {
        return (TryLoadData) { .error = VfsResolveError_InvalidPath };
    }

    const FlVfsPlugin* plugin = plugin_entry->plugin;

    for (int try_segments = range_start; try_segments > range_end; try_segments--) {
        const FlString try_path = vfs_path_range(components, range_end, try_segments, scratch);

        logc_trace(VFS_ID, "%S: trying to open %S", plugin->plugin_name, try_path);

        if (!plugin->can_open(try_path)) {
            logc_trace(VFS_ID, "%S: trying to open %S (failed)", plugin->plugin_name, try_path);
            continue;
        }

        logc_trace(VFS_ID, "Trying to open %S", try_path);

        void* instance = plugin_entry->plugin_instance;
        FlVfsOpenResult open_result = plugin->open(instance, parent_handle, try_path, 0, (FlVfsMountOptions) { 0 });

        if (!open_result.handle) {
            continue;
        }

        void* handle = open_result.handle;

        // Only return directly if top path is a directory
        if (plugin->is_directory(instance, handle) && try_segments == range_start) {
            logc_debug(VFS_ID, "is dir");
            return (TryLoadData) { .handle = handle,
                                   .path_split_offset = try_segments,
                                   .loaded_path = fl_string_copy(scratch, try_path),
                                   .is_directory = true };
        }

        FlVfsSizeResult size_result = plugin->get_size(instance, handle, (FlString) { 0 });

        if (size_result.size < 0) {
            close_plugin_handle(plugin, instance, handle);
            continue;
        }

        return (TryLoadData) { .handle = handle,
                               .path_split_offset = try_segments,
                               .size = size_result.size,
                               .loaded_path = fl_string_copy(scratch, try_path) };
    }

    logc_trace(VFS_ID, "not found");

    return (TryLoadData) { .error = VfsResolveError_FileNotFound };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DriverHandle vfs_resolve_find_root_driver(VfsState* self, FlArena* temp, const PathComponents* comp, int start) {
    profile_function_auto_nc("vfs:vfs_resolve_find_root_driver", PROFILE_COLOR_CYAN);
    int end = comp->segments.count;

    for (int try_segments = end; try_segments > start; try_segments--) {
        const FlString try_path = path_reconstruct_from_segments(comp, start, try_segments, temp);

        for (VfsPluginEntry* entry = self->plugin_first; entry != nullptr; entry = entry->next) {
            const FlVfsPlugin* plugin = entry->plugin;
            FlVfsOpenResult open_result
                = plugin->open(entry->plugin_instance, nullptr, try_path, 0, (FlVfsMountOptions) { 0 });

            if (!open_result.handle) {
                continue;
            }

            logc_trace(VFS_ID, "Found root at %S plugin %S", try_path, plugin->plugin_name);

            return (DriverHandle) { .plugin_entry = entry, .handle = open_result.handle, .path_index = try_segments };
        }
    }

    logc_trace(VFS_ID, "not found");

    return (DriverHandle) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool driver_opens_from_memory(const FlVfsPlugin* plugin) {
    return (plugin->get_capabilities() & FlVfsDriverCapabilities_Memory) && plugin->open_memory;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DriverHandle vfs_resolve_find_memory_driver(VfsState* self, FlString org_path, u8* data, u64 size) {
    profile_function_auto_nc("vfs:vfs_resolve_find_memory_driver", PROFILE_COLOR_CYAN);
    logc_trace(VFS_ID, "%S", org_path);

    for (VfsPluginEntry* entry = self->plugin_first; entry != nullptr; entry = entry->next) {
        const FlVfsPlugin* plugin = entry->plugin;

        if (!driver_opens_from_memory(plugin)) {
            continue;
        }

        FlVfsOpenResult open_result = plugin->open_memory(entry->plugin_instance, data, size, org_path);

        if (open_result.handle) {
            return (DriverHandle) { .plugin_entry = entry, .handle = open_result.handle };
        }

        if (open_result.error.vfs_status == FlVfsStatus_ArchiveCorrupt) {
            return (DriverHandle) { 0 };
        }
    }

    return (DriverHandle) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool has_memory_driver(VfsState* self) {
    for (VfsPluginEntry* entry = self->plugin_first; entry != nullptr; entry = entry->next) {
        if (driver_opens_from_memory(entry->plugin)) {
            return true;
        }
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Reads the whole file behind handle and offers it to the memory drivers as an archive. loaded_path only names the
// data for driver selection. The memory driver owns the buffer once it has opened it; in every other case it is freed.
// Reads nothing, and reports no archive, when no memory driver is registered; a file over VFS_MAX_READ_SIZE is an
// error.
static MemoryArchive open_archive_in_memory(VfsState* self, VfsPluginEntry* entry, void* handle, FlString read_path,
                                            FlString loaded_path, i64 size) {
    MemoryArchive archive = { 0 };

    if (!has_memory_driver(self)) {
        return archive;
    }

    if (size > VFS_MAX_READ_SIZE) {
        archive.error = VfsResolveError_FileTooLarge;
        return archive;
    }

    u8* buffer = mi_malloc(size);
    if (!buffer) {
        archive.error = VfsResolveError_OutOfMemory;
        return archive;
    }

    FlVfsReadResult read_result = entry->plugin->read(entry->plugin_instance, handle, read_path, buffer, size);

    if (read_result.bytes_read != size) {
        mi_free(buffer);
        archive.error = VfsResolveError_InvalidFileSize;
        return archive;
    }

    archive.opened = vfs_resolve_find_memory_driver(self, loaded_path, buffer, size);

    if (!archive.opened.handle) {
        mi_free(buffer);
    }

    return archive;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Path resolution

FinalDriver vfs_resolve_from_root(VfsState* self, VfsPluginEntry* root_entry, void* root_handle,
                                  PathComponents* components, int path_start, FlArena* scratch) {
    VfsPluginEntry* current_entry = root_entry;
    const FlVfsPlugin* current_plugin = root_entry->plugin;
    void* current_instance = current_entry->plugin_instance;
    void* current_handle = root_handle;
    int current_pos = path_start;
    const int total_segments = components->segments.count;

    // The root handle consumed every segment: when it is a file, open it as an archive.
    if (current_pos >= total_segments && !current_plugin->is_directory(current_instance, current_handle)) {
        FlVfsSizeResult size_result = current_plugin->get_size(current_instance, current_handle, (FlString) { 0 });
        if (size_result.size < 0) {
            close_plugin_handle(current_plugin, current_instance, current_handle);
            return (FinalDriver) { .error = VfsResolveError_InvalidFileSize };
        }

        FlString loaded_path = path_reconstruct_from_segments(components, 0, total_segments, scratch);
        MemoryArchive archive = open_archive_in_memory(self, current_entry, current_handle, (FlString) { 0 },
                                                       loaded_path, size_result.size);

        close_plugin_handle(current_plugin, current_instance, current_handle);

        if (archive.error != VfsResolveError_None) {
            return (FinalDriver) { .error = archive.error };
        }

        if (!archive.opened.handle) {
            // No memory driver took it
            return (FinalDriver) { .error = VfsResolveError_NotMountable };
        }

        current_entry = archive.opened.plugin_entry;
        current_plugin = current_entry->plugin;
        current_instance = current_entry->plugin_instance;
        current_handle = archive.opened.handle;
    }

    while (current_pos < total_segments) {
        // Scans backwards from the end for the longest path that opens
        TryLoadData data
            = try_open_file(current_entry, current_handle, total_segments, current_pos, components, scratch);

        if (data.error != VfsResolveError_None) {
            close_plugin_handle(current_plugin, current_instance, current_handle);
            return (FinalDriver) { .error = data.error };
        }

        if (data.is_directory) {
            // A directory that consumed the whole remaining path is the final layer.
            close_plugin_handle(current_plugin, current_instance, current_handle);
            return (
                FinalDriver) { .plugin_entry = current_entry, .handle = data.handle, .error = VfsResolveError_None };
        }

        // A file: open it as an archive with the memory drivers
        MemoryArchive archive
            = open_archive_in_memory(self, current_entry, data.handle, (FlString) { 0 }, data.loaded_path, data.size);

        close_plugin_handle(current_plugin, current_instance, data.handle);
        close_plugin_handle(current_plugin, current_instance, current_handle);

        if (archive.error != VfsResolveError_None) {
            return (FinalDriver) { .error = archive.error };
        }

        if (!archive.opened.handle) {
            // No memory driver took it
            return (FinalDriver) { .error = VfsResolveError_NotMountable };
        }

        current_entry = archive.opened.plugin_entry;
        current_plugin = current_entry->plugin;
        current_instance = current_entry->plugin_instance;
        current_handle = archive.opened.handle;
        current_pos = data.path_split_offset;
    }

    // The final layer must be a directory or archive
    if (!current_plugin->is_directory(current_instance, current_handle)) {
        close_plugin_handle(current_plugin, current_instance, current_handle);
        return (FinalDriver) { .error = VfsResolveError_NotMountable };
    }

    return (FinalDriver) { .plugin_entry = current_entry, .handle = current_handle, .error = VfsResolveError_None };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FinalDriver vfs_resolve_mount_path(VfsState* self, FlString path, FlArena* scratch) {
    profile_function_auto_nc("vfs:vfs_resolve_mount_path", PROFILE_COLOR_CYAN);

    PathComponents components = path_components(scratch, path);
    DriverHandle root_plugin = vfs_resolve_find_root_driver(self, scratch, &components, 0);

    if (!root_plugin.plugin_entry) {
        logc_trace(VFS_ID, "No root plugin found for path: %S", path);
        return (FinalDriver) { .error = VfsResolveError_NoDriver };
    }

    return vfs_resolve_from_root(self, root_plugin.plugin_entry, root_plugin.handle, &components,
                                 root_plugin.path_index, scratch);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Archives below a mount's root

bool vfs_name_is_archive(VfsState* self, FlString name) {
    for (VfsPluginEntry* entry = self->plugin_first; entry != nullptr; entry = entry->next) {
        if (entry == self->builtin_fallback) {
            continue;
        }

        const FlVfsPlugin* plugin = entry->plugin;
        if ((plugin->get_capabilities() & FlVfsDriverCapabilities_Archive) && plugin->supports_path(name)) {
            return true;
        }
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Opens a file on disk in place with the first archive driver that accepts it.
static DriverHandle open_archive_file(VfsState* self, FlString file_path) {
    for (VfsPluginEntry* entry = self->plugin_first; entry != nullptr; entry = entry->next) {
        const FlVfsPlugin* plugin = entry->plugin;

        if (entry == self->builtin_fallback || !plugin->can_open(file_path)) {
            continue;
        }

        FlVfsOpenResult open_result
            = plugin->open(entry->plugin_instance, nullptr, file_path, 0, (FlVfsMountOptions) { 0 });
        if (!open_result.handle) {
            continue;
        }

        if (!plugin->is_directory(entry->plugin_instance, open_result.handle)) {
            close_plugin_handle(plugin, entry->plugin_instance, open_result.handle);
            continue;
        }

        return (DriverHandle) { .plugin_entry = entry, .handle = open_result.handle };
    }

    return (DriverHandle) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DriverHandle vfs_resolve_open_archive(VfsState* self, VfsPluginEntry* parent, void* parent_handle, FlString path,
                                      i64 size, FlArena* scratch) {
    const FlVfsPlugin* plugin = parent->plugin;

    if (size <= 0) {
        return (DriverHandle) { 0 };
    }

    if ((plugin->get_capabilities() & FlVfsDriverCapabilities_DirectPath) && plugin->get_file_path) {
        FlString file_path = plugin->get_file_path(parent->plugin_instance, parent_handle, path, scratch);
        DriverHandle opened = file_path.length > 0 ? open_archive_file(self, file_path) : (DriverHandle) { 0 };

        if (opened.handle) {
            return opened;
        }
    }

    return open_archive_in_memory(self, parent, parent_handle, path, path, size).opened;
}
