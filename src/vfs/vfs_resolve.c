// Resolves a mount's source path to a driver and one root handle, descending through nested archives.

#include <core/log.h>
#include <core/memory.h>
#include <core/path.h>
#include <core/profile.h>
#include <core/string.h>
#include "vfs_private.h"

// Closes a plugin handle; every call site passes a live one.

static void close_plugin_handle(const FlVfsPlugin* plugin, void* instance, void* handle) {
    FL_VALIDATE(handle != nullptr);
    plugin->close(instance, handle);
}

static void close_driver_handle(DriverHandle driver) {
    close_plugin_handle(driver.plugin_entry->plugin, driver.plugin_entry->plugin_instance, driver.handle);
}

static bool driver_handle_is_directory(DriverHandle driver) {
    return driver.plugin_entry->plugin->is_directory(driver.plugin_entry->plugin_instance, driver.handle);
}

// Plugin utilities

FlString vfs_path_range(const PathComponents* components, int start, u32 end, FlArena* arena) {
    PathComponents range = *components;
    if (start > 0) {
        range.is_absolute = false;
    }
    return path_reconstruct_from_segments(&range, start, end, arena);
}

typedef struct {
    DriverHandle opened;
    int error;
} MemoryArchive;

// Opens try_path below parent; a null handle when the plugin does not take it.
static void* open_candidate(DriverHandle parent, FlString try_path) {
    const FlVfsPlugin* plugin = parent.plugin_entry->plugin;

    logc_trace(VFS_ID, "%S: trying to open %S", plugin->plugin_name, try_path);

    if (!plugin->can_open(try_path)) {
        logc_trace(VFS_ID, "%S: trying to open %S (failed)", plugin->plugin_name, try_path);
        return nullptr;
    }

    logc_trace(VFS_ID, "Trying to open %S", try_path);

    return plugin->open(parent.plugin_entry->plugin_instance, parent.handle, try_path, 0, (FlVfsMountOptions) { 0 })
        .handle;
}

// Describes a file just opened; closes it and returns a null handle when its size is unknown.
static TryLoadData describe_opened_file(DriverHandle opened, FlString try_path, FlArena* scratch) {
    const FlVfsPlugin* plugin = opened.plugin_entry->plugin;
    FlVfsSizeResult size_result
        = plugin->get_size(opened.plugin_entry->plugin_instance, opened.handle, (FlString) { 0 });

    if (size_result.size < 0) {
        close_driver_handle(opened);
        return (TryLoadData) { 0 };
    }

    return (TryLoadData) { .handle = opened.handle,
                           .path_split_offset = opened.path_index,
                           .size = size_result.size,
                           .loaded_path = fl_string_copy(scratch, try_path) };
}

static bool opened_whole_path_directory(DriverHandle opened, int range_start) {
    return driver_handle_is_directory(opened) && opened.path_index == range_start;
}

// Scans backwards from the end of the path for the longest range, starting at parent.path_index, that opens.
static TryLoadData try_open_file(DriverHandle parent, const PathComponents* components, FlArena* scratch) {
    if (!parent.plugin_entry || components->segments.count == 0) {
        return (TryLoadData) { .error = VfsResolveError_InvalidPath };
    }

    const int range_start = components->segments.count;
    const int range_end = parent.path_index;

    for (int try_segments = range_start; try_segments > range_end; try_segments--) {
        const FlString try_path = vfs_path_range(components, range_end, try_segments, scratch);
        void* handle = open_candidate(parent, try_path);

        if (!handle) {
            continue;
        }

        const DriverHandle opened
            = { .plugin_entry = parent.plugin_entry, .handle = handle, .path_index = try_segments };

        // Only return directly if top path is a directory
        if (opened_whole_path_directory(opened, range_start)) {
            logc_debug(VFS_ID, "is dir");
            return (TryLoadData) { .handle = handle,
                                   .path_split_offset = try_segments,
                                   .loaded_path = fl_string_copy(scratch, try_path),
                                   .is_directory = true };
        }

        TryLoadData data = describe_opened_file(opened, try_path, scratch);

        if (data.handle) {
            return data;
        }
    }

    logc_trace(VFS_ID, "not found");

    return (TryLoadData) { .error = VfsResolveError_FileNotFound };
}

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

static bool driver_opens_from_memory(const FlVfsPlugin* plugin) {
    return (plugin->get_capabilities() & FlVfsDriverCapabilities_Memory) && plugin->open_memory;
}

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

static bool has_memory_driver(VfsState* self) {
    for (VfsPluginEntry* entry = self->plugin_first; entry != nullptr; entry = entry->next) {
        if (driver_opens_from_memory(entry->plugin)) {
            return true;
        }
    }

    return false;
}

// Reads the whole file behind file->container and offers it to the memory drivers as an archive. loaded_path only
// names the data for driver selection. The memory driver owns the buffer once it has opened it; in every other case
// it is freed. Reads nothing, and reports no archive, when no memory driver is registered; a file over
// VFS_MAX_READ_SIZE is an error.
static MemoryArchive open_archive_in_memory(VfsState* self, const ArchiveFile* file, FlString loaded_path) {
    MemoryArchive archive = { 0 };

    if (!has_memory_driver(self)) {
        return archive;
    }

    if (file->size > VFS_MAX_READ_SIZE) {
        archive.error = VfsResolveError_FileTooLarge;
        return archive;
    }

    u8* buffer = mi_malloc(file->size);
    if (!buffer) {
        archive.error = VfsResolveError_OutOfMemory;
        return archive;
    }

    const VfsPluginEntry* entry = file->container.plugin_entry;
    FlVfsReadResult read_result
        = entry->plugin->read(entry->plugin_instance, file->container.handle, file->path, buffer, file->size);

    if (read_result.bytes_read != file->size) {
        mi_free(buffer);
        archive.error = VfsResolveError_InvalidFileSize;
        return archive;
    }

    archive.opened = vfs_resolve_find_memory_driver(self, loaded_path, buffer, file->size);

    if (!archive.opened.handle) {
        mi_free(buffer);
    }

    return archive;
}

// Turns "no memory driver took it" into an error.
static MemoryArchive require_archive(MemoryArchive archive) {
    if (archive.error == VfsResolveError_None && !archive.opened.handle) {
        archive.error = VfsResolveError_NotMountable;
    }

    return archive;
}

// Path resolution

// The root handle consumed every segment and is a file: opens it as an archive. Consumes the root handle.
static MemoryArchive open_root_file_as_archive(VfsState* self, DriverHandle root, const PathComponents* components,
                                               FlArena* scratch) {
    const VfsPluginEntry* entry = root.plugin_entry;
    FlVfsSizeResult size_result = entry->plugin->get_size(entry->plugin_instance, root.handle, (FlString) { 0 });

    if (size_result.size < 0) {
        close_driver_handle(root);
        return (MemoryArchive) { .error = VfsResolveError_InvalidFileSize };
    }

    const ArchiveFile file = { .container = root, .size = size_result.size };
    FlString loaded_path = path_reconstruct_from_segments(components, 0, components->segments.count, scratch);
    MemoryArchive archive = open_archive_in_memory(self, &file, loaded_path);

    close_driver_handle(root);
    return require_archive(archive);
}

// Opens the file found by try_open_file as an archive. Consumes data.handle and the container.
static MemoryArchive open_found_file_as_archive(VfsState* self, DriverHandle container, const TryLoadData* data) {
    const ArchiveFile file
        = { .container = { .plugin_entry = container.plugin_entry, .handle = data->handle }, .size = data->size };
    MemoryArchive archive = open_archive_in_memory(self, &file, data->loaded_path);

    close_driver_handle(file.container);
    close_driver_handle(container);
    return require_archive(archive);
}

static FinalDriver final_driver(DriverHandle driver) {
    return (
        FinalDriver) { .plugin_entry = driver.plugin_entry, .handle = driver.handle, .error = VfsResolveError_None };
}

typedef struct {
    DriverHandle layer;
    int error;
    bool is_final;
} Descent;

// Opens the next layer below current, consuming current. A directory that took the whole remaining path is final.
static Descent descend_one_layer(VfsState* self, DriverHandle current, const PathComponents* components,
                                 FlArena* scratch) {
    TryLoadData data = try_open_file(current, components, scratch);

    if (data.error != VfsResolveError_None) {
        close_driver_handle(current);
        return (Descent) { .error = data.error };
    }

    if (data.is_directory) {
        close_driver_handle(current);
        return (Descent) { .layer = { .plugin_entry = current.plugin_entry, .handle = data.handle }, .is_final = true };
    }

    MemoryArchive archive = open_found_file_as_archive(self, current, &data);
    archive.opened.path_index = data.path_split_offset;
    return (Descent) { .layer = archive.opened, .error = archive.error };
}

static Descent descend_root_file(VfsState* self, DriverHandle root, const PathComponents* components,
                                 FlArena* scratch) {
    MemoryArchive archive = open_root_file_as_archive(self, root, components, scratch);
    archive.opened.path_index = root.path_index;
    return (Descent) { .layer = archive.opened, .error = archive.error };
}

// The final layer must be a directory or archive.
static FinalDriver final_from_descent(Descent step) {
    if (step.error != VfsResolveError_None) {
        return (FinalDriver) { .error = step.error };
    }

    if (!step.is_final && !driver_handle_is_directory(step.layer)) {
        close_driver_handle(step.layer);
        return (FinalDriver) { .error = VfsResolveError_NotMountable };
    }

    return final_driver(step.layer);
}

static bool descent_continues(const Descent* step, int total_segments) {
    if (step->error != VfsResolveError_None || step->is_final) {
        return false;
    }

    return step->layer.path_index < total_segments;
}

FinalDriver vfs_resolve_from_root(VfsState* self, DriverHandle root, PathComponents* components, FlArena* scratch) {
    const int total_segments = components->segments.count;
    Descent step = { .layer = root };

    if (root.path_index >= total_segments && !driver_handle_is_directory(root)) {
        step = descend_root_file(self, root, components, scratch);
    }

    while (descent_continues(&step, total_segments)) {
        step = descend_one_layer(self, step.layer, components, scratch);
    }

    return final_from_descent(step);
}

FinalDriver vfs_resolve_mount_path(VfsState* self, FlString path, FlArena* scratch) {
    profile_function_auto_nc("vfs:vfs_resolve_mount_path", PROFILE_COLOR_CYAN);

    PathComponents components = path_components(scratch, path);
    DriverHandle root_plugin = vfs_resolve_find_root_driver(self, scratch, &components, 0);

    if (!root_plugin.plugin_entry) {
        logc_trace(VFS_ID, "No root plugin found for path: %S", path);
        return (FinalDriver) { .error = VfsResolveError_NoDriver };
    }

    return vfs_resolve_from_root(self, root_plugin, &components, scratch);
}

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

// Opens a file on disk in place with one archive driver, if it accepts the file.
static DriverHandle open_archive_with(VfsPluginEntry* entry, FlString file_path) {
    const FlVfsPlugin* plugin = entry->plugin;

    if (!plugin->can_open(file_path)) {
        return (DriverHandle) { 0 };
    }

    FlVfsOpenResult open_result
        = plugin->open(entry->plugin_instance, nullptr, file_path, 0, (FlVfsMountOptions) { 0 });

    if (!open_result.handle) {
        return (DriverHandle) { 0 };
    }

    if (!plugin->is_directory(entry->plugin_instance, open_result.handle)) {
        close_plugin_handle(plugin, entry->plugin_instance, open_result.handle);
        return (DriverHandle) { 0 };
    }

    return (DriverHandle) { .plugin_entry = entry, .handle = open_result.handle };
}

// Opens a file on disk in place with the first archive driver that accepts it.
static DriverHandle open_archive_file(VfsState* self, FlString file_path) {
    for (VfsPluginEntry* entry = self->plugin_first; entry != nullptr; entry = entry->next) {
        if (entry == self->builtin_fallback) {
            continue;
        }

        DriverHandle opened = open_archive_with(entry, file_path);

        if (opened.handle) {
            return opened;
        }
    }

    return (DriverHandle) { 0 };
}

DriverHandle vfs_resolve_open_archive(VfsState* self, const ArchiveFile* file, FlArena* scratch) {
    const VfsPluginEntry* parent = file->container.plugin_entry;
    const FlVfsPlugin* plugin = parent->plugin;

    if (file->size <= 0) {
        return (DriverHandle) { 0 };
    }

    if ((plugin->get_capabilities() & FlVfsDriverCapabilities_DirectPath) && plugin->get_file_path) {
        FlString file_path
            = plugin->get_file_path(parent->plugin_instance, file->container.handle, file->path, scratch);
        DriverHandle opened = file_path.length > 0 ? open_archive_file(self, file_path) : (DriverHandle) { 0 };

        if (opened.handle) {
            return opened;
        }
    }

    return open_archive_in_memory(self, file, file->path).opened;
}
