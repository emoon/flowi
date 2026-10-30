// VFS Tree
// The nodes met while resolving paths below a mount, and the archives they carry

#include <core/arena.h>
#include <core/assert.h>
#include <core/path.h>
#include <core/profile.h>
#include <core/string.h>
#include "vfs_private.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static VfsTreeNode* find_child_node(const VfsTreeNode* parent, FlString name) {
    for (VfsTreeNode* child = parent->first_child; child != nullptr; child = child->next_sibling) {
        if (fl_string_equals(child->name, name)) {
            return child;
        }
    }

    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void close_node_handle(VfsTreeNode* node) {
    if (node->handle) {
        node->plugin_entry->plugin->close(node->plugin_entry->plugin_instance, node->handle);
        node->handle = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void vfs_tree_cleanup_node(VfsTreeNode* node) {
    profile_function_auto_nc("vfs:vfs_tree_cleanup_node", PROFILE_COLOR_CYAN);
    FL_VALIDATE(node != nullptr);

    for (VfsTreeNode* child = node->first_child; child != nullptr; child = child->next_sibling) {
        vfs_tree_cleanup_node(child);
    }

    close_node_handle(node);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Archive loading

bool vfs_tree_try_load_as_archive(VfsState* self, VfsTreeNode* node, VfsPluginEntry* parent, void* parent_handle,
                                  FlString path, i64 size, FlArena* scratch) {
    DriverHandle opened = vfs_resolve_open_archive(self, parent, parent_handle, path, size, scratch);

    if (!opened.handle) {
        return false;
    }

    node->plugin_entry = opened.plugin_entry;
    node->handle = opened.handle;
    node->is_directory = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Path resolution

VfsLocation vfs_tree_locate(VfsState* self, FlVfsMount* mount, FlString path, bool is_directory, FlArena* scratch) {
    profile_function_auto_nc("vfs:vfs_tree_locate", PROFILE_COLOR_CYAN);

    PathComponents components = path_components(scratch, path);
    const int count = components.segments.count;
    // The last segment of a file path is the file itself, which is never entered.
    const int enterable = is_directory || count == 0 ? count : count - 1;

    VfsPluginEntry* driver = mount->root_node->plugin_entry;
    void* handle = mount->root_node->handle;

    // An absolute path names a different place than a relative one with the same segments, so it gets its own subtree.
    VfsTreeNode* node = mount->root_node;
    if (components.is_absolute) {
        VfsTreeNode* absolute = find_child_node(node, FL_S("/"));
        node = absolute ? absolute : vfs_tree_create_node(mount, node, FL_S("/"));
    }

    int since_driver = 0; // Entered segments between the node that carries the driver and the current one

    for (int i = 0; i < enterable; i++) {
        FlString segment = components.segments.items[i];
        VfsTreeNode* child = find_child_node(node, segment);

        if (!child) {
            child = vfs_tree_create_node(mount, node, segment);
        }

        node = child;

        // Walk down onto an archive that is already open.
        if (node->plugin_entry) {
            driver = node->plugin_entry;
            handle = node->handle;
            since_driver = 0;
            continue;
        }

        if (!node->is_directory) {
            FlString below = vfs_path_range(&components, i - since_driver, i + 1, scratch);
            FlVfsSizeResult size_result = driver->plugin->get_size(driver->plugin_instance, handle, below);

            if (size_result.size < 0) {
                // Not a file the driver knows: a directory, or a virtual one such as a part of a URL. Remembered so
                // the driver is not asked again.
                node->is_directory = true;
            } else if (vfs_tree_try_load_as_archive(self, node, driver, handle, below, size_result.size, scratch)) {
                driver = node->plugin_entry;
                handle = node->handle;
                since_driver = 0;
                continue;
            } else {
                // A file that is no archive, or one that does not open: the path goes on below it as a plain path,
                // and it is not read again.
                node->is_directory = true;
            }
        }

        since_driver++;
    }

    return (VfsLocation) {
        .driver = driver,
        .handle = handle,
        .path = vfs_path_range(&components, enterable - since_driver, count, scratch),
    };
}
