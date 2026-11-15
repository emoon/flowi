#include "tree_flat.h"
#include <flowi/arena/arena_macros.h>
#include <flowi/core/assert.h>
#include <flowi/core/utils.h>

typedef struct OpenNode {
    i32 node;
    u32 next_child;
    u32 child_count;
} OpenNode;

typedef struct FlatBuild {
    TreeFlat flat;
    OpenNode* open;
    u32 open_count;
} FlatBuild;

static void push_range(FlatBuild* build, u32 level, u32 end_child, i32 last_row_node) {
    OpenNode* open = &build->open[level];
    if (end_child <= open->next_child) {
        return;
    }
    TreeFlat* flat = &build->flat;
    u32 count = end_child - open->next_child;
    flat->ranges[flat->range_count++] = (TreeFlatRange) {
        .first_row = flat->row_count,
        .count = count,
        .first_child = open->next_child,
        .node = open->node,
        .last_row_node = last_row_node,
        .depth = (u16)level,
    };
    flat->row_count += count;
    open->next_child = end_child;
}

static void close_to_level(FlatBuild* build, u32 level) {
    while (build->open_count > level + 1) {
        u32 top = build->open_count - 1;
        push_range(build, top, build->open[top].child_count, -1);
        build->open_count--;
    }
}

static i32 open_level_of(const FlatBuild* build, i32 node) {
    for_count(i, build->open_count) {
        if (build->open[i].node == node) {
            return (i32)i;
        }
    }
    return -1;
}

static bool child_fits(const OpenNode* open, u32 child_index) {
    return child_index >= open->next_child && child_index < open->child_count;
}

static void open_node(FlatBuild* build, const FlTreeExpanded* nodes, u32 index, u64* rows_left) {
    const FlTreeExpanded* node = &nodes[index];
    i32 level = open_level_of(build, node->parent);
    bool has_level = level >= 0 && level < UINT16_MAX;
    bool fits = has_level && child_fits(&build->open[level], node->child_index);
    bool valid = fits && node->child_count <= *rows_left;
    FL_ASSERT_DEBUG(valid && "expanded nodes must be in display order and fit their parent");
    if (!valid) {
        return;
    }
    close_to_level(build, (u32)level);
    push_range(build, (u32)level, node->child_index + 1, (i32)index);
    build->open[build->open_count++] = (OpenNode) { .node = (i32)index, .child_count = node->child_count };
    *rows_left -= node->child_count;
}

TreeFlat tree_flat_build(FlArena* arena, u32 root_count, const FlTreeExpanded* nodes, u32 node_count) {
    u32 used_nodes = nodes != nullptr ? node_count : 0;
    FlatBuild build = {
        .flat = { .nodes = nodes, .ranges = arena_alloc_array(arena, TreeFlatRange, (u64)used_nodes * 2 + 1) },
        .open = arena_alloc_array(arena, OpenNode, (u64)used_nodes + 1),
        .open_count = 1,
    };
    u32 root_rows = root_count < (u32)INT32_MAX ? root_count : (u32)INT32_MAX;
    build.open[0] = (OpenNode) { .node = -1, .child_count = root_rows };
    u64 rows_left = (u64)INT32_MAX - root_rows;

    for_count(i, used_nodes) {
        open_node(&build, nodes, i, &rows_left);
    }
    close_to_level(&build, 0);
    push_range(&build, 0, root_rows, -1);
    return build.flat;
}

static u32 range_index_at(const TreeFlat* flat, u32 row) {
    u32 lo = 0;
    u32 hi = flat->range_count;
    while (hi - lo > 1) {
        u32 mid = lo + (hi - lo) / 2;
        if (flat->ranges[mid].first_row <= row) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

FlTreeVirtualRow tree_flat_row(const TreeFlat* flat, u32 row) {
    FL_ASSERT(row < flat->row_count);
    const TreeFlatRange* range = &flat->ranges[range_index_at(flat, row)];
    u32 offset = row - range->first_row;
    bool is_last = offset + 1 == range->count;
    return (FlTreeVirtualRow) {
        .parent = range->node,
        .child_index = range->first_child + offset,
        .depth = range->depth,
        .node = is_last ? range->last_row_node : -1,
    };
}

static bool range_holds(const TreeFlatRange* range, i32 parent, u32 child_index) {
    bool at_or_after_first = range->node == parent && child_index >= range->first_child;
    return at_or_after_first && child_index - range->first_child < range->count;
}

i32 tree_flat_row_of(const TreeFlat* flat, i32 parent, u32 child_index) {
    for_count(i, flat->range_count) {
        const TreeFlatRange* range = &flat->ranges[i];
        if (range_holds(range, parent, child_index)) {
            return (i32)(range->first_row + (child_index - range->first_child));
        }
    }
    return -1;
}

u32 tree_flat_parent_row(const TreeFlat* flat, u32 row) {
    FlTreeVirtualRow info = tree_flat_row(flat, row);
    if (info.parent < 0) {
        return row;
    }
    const FlTreeExpanded* parent = &flat->nodes[info.parent];
    return (u32)tree_flat_row_of(flat, parent->parent, parent->child_index);
}

u32 tree_flat_subtree_end(const TreeFlat* flat, u32 row) {
    u32 index = range_index_at(flat, row);
    const TreeFlatRange* range = &flat->ranges[index];
    bool is_last = row - range->first_row + 1 == range->count;
    if (!is_last) {
        return row + 1;
    }
    for (u32 i = index + 1; i < flat->range_count; i++) {
        if (flat->ranges[i].depth <= range->depth) {
            return flat->ranges[i].first_row;
        }
    }
    return flat->row_count;
}
