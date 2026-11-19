// The nodes met while resolving paths below a mount, and the archives they carry.

#include <core/arena.h>
#include <core/assert.h>
#include <core/path.h>
#include <core/profile.h>
#include <core/string.h>
#include "vfs_private.h"

// Nodes

VfsTreeNode* vfs_tree_create_node(FlVfsMount* mount, VfsTreeNode* parent, FlString name) {
    VfsTreeNode* node = arena_alloc_zero(mount->nodes_arena, VfsTreeNode);
    node->name = fl_string_copy(mount->nodes_arena, name);
    node->parent = parent;

    if (parent) {
        node->next_sibling = parent->first_child;
        parent->first_child = node;
    }

    return node;
}

static VfsTreeNode* find_child_node(const VfsTreeNode* parent, FlString name) {
    for (VfsTreeNode* child = parent->first_child; child != nullptr; child = child->next_sibling) {
        if (fl_string_equals(child->name, name)) {
            return child;
        }
    }

    return nullptr;
}

static void close_node_handle(VfsTreeNode* node) {
    if (node->handle) {
        node->plugin_entry->plugin->close(node->plugin_entry->plugin_instance, node->handle);
        node->handle = nullptr;
    }
}

void vfs_tree_cleanup_node(VfsTreeNode* node) {
    profile_function_auto_nc("vfs:vfs_tree_cleanup_node", PROFILE_COLOR_CYAN);
    FL_VALIDATE(node != nullptr);

    for (VfsTreeNode* child = node->first_child; child != nullptr; child = child->next_sibling) {
        vfs_tree_cleanup_node(child);
    }

    close_node_handle(node);
}

// Archive loading

bool vfs_tree_try_load_as_archive(VfsState* self, VfsTreeNode* node, const ArchiveFile* file, FlArena* scratch) {
    DriverHandle opened = vfs_resolve_open_archive(self, file, scratch);

    if (!opened.handle) {
        return false;
    }

    node->plugin_entry = opened.plugin_entry;
    node->handle = opened.handle;
    node->is_directory = true;
    return true;
}

// Path resolution

typedef struct {
    VfsState* self;
    FlArena* scratch;
    const PathComponents* components;
    VfsPluginEntry* driver;
    void* handle;
    int since_driver; // Entered segments between the node that carries the driver and the current one
} Locator;

static VfsTreeNode* find_or_create_child(FlVfsMount* mount, VfsTreeNode* parent, FlString name) {
    VfsTreeNode* child = find_child_node(parent, name);
    return child ? child : vfs_tree_create_node(mount, parent, name);
}

static void adopt_node_driver(Locator* locator, const VfsTreeNode* node) {
    locator->driver = node->plugin_entry;
    locator->handle = node->handle;
    locator->since_driver = 0;
}

// Asks the driver about a node not yet known to be a directory or an archive, and records what it is.
static void probe_file_node(Locator* locator, VfsTreeNode* node, int index) {
    FlString below = vfs_path_range(locator->components, index - locator->since_driver, index + 1, locator->scratch);
    FlVfsSizeResult size_result
        = locator->driver->plugin->get_size(locator->driver->plugin_instance, locator->handle, below);

    if (size_result.size < 0) {
        // Not a file the driver knows: a directory, or a virtual one such as a part of a URL. Remembered so
        // the driver is not asked again.
        node->is_directory = true;
        return;
    }

    const ArchiveFile file = { .container = { .plugin_entry = locator->driver, .handle = locator->handle },
                               .path = below,
                               .size = size_result.size };

    if (!vfs_tree_try_load_as_archive(locator->self, node, &file, locator->scratch)) {
        // A file that is no archive, or one that does not open: the path goes on below it as a plain path,
        // and it is not read again.
        node->is_directory = true;
    }
}

static void enter_node(Locator* locator, VfsTreeNode* node, int index) {
    if (!node->plugin_entry && !node->is_directory) {
        probe_file_node(locator, node, index);
    }

    // Walk down onto an archive, whether it was already open or just was.
    if (node->plugin_entry) {
        adopt_node_driver(locator, node);
        return;
    }

    locator->since_driver++;
}

// An absolute path names a different place than a relative one with the same segments, so it gets its own subtree.
static VfsTreeNode* locate_start_node(FlVfsMount* mount, const PathComponents* components) {
    if (!components->is_absolute) {
        return mount->root_node;
    }

    return find_or_create_child(mount, mount->root_node, FL_S("/"));
}

VfsLocation vfs_tree_locate(VfsState* self, FlVfsMount* mount, FlString path, bool is_directory, FlArena* scratch) {
    profile_function_auto_nc("vfs:vfs_tree_locate", PROFILE_COLOR_CYAN);

    PathComponents components = path_components(scratch, path);
    const int count = components.segments.count;
    // The last segment of a file path is the file itself, which is never entered.
    const int enterable = is_directory || count == 0 ? count : count - 1;

    Locator locator = { .self = self,
                        .scratch = scratch,
                        .components = &components,
                        .driver = mount->root_node->plugin_entry,
                        .handle = mount->root_node->handle };

    VfsTreeNode* node = locate_start_node(mount, &components);

    for (int i = 0; i < enterable; i++) {
        node = find_or_create_child(mount, node, components.segments.items[i]);
        enter_node(&locator, node, i);
    }

    return (VfsLocation) {
        .driver = locator.driver,
        .handle = locator.handle,
        .path = vfs_path_range(&components, enterable - locator.since_driver, count, scratch),
    };
}
