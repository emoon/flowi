#pragma once

#include <core/types.h>
#include <flowi/ui/tree.h>

typedef struct FlArena FlArena;

typedef struct TreeFlatRange {
    u32 first_row;
    u32 count;
    u32 first_child;
    i32 node;
    i32 last_row_node;
    u16 depth;
} TreeFlatRange;

typedef struct TreeFlat {
    const FlTreeExpanded* nodes;
    TreeFlatRange* ranges;
    u32 range_count;
    u32 row_count;
} TreeFlat;

TreeFlat tree_flat_build(FlArena* arena, u32 root_count, const FlTreeExpanded* nodes, u32 node_count);

FlTreeVirtualRow tree_flat_row(const TreeFlat* flat, u32 row);

i32 tree_flat_row_of(const TreeFlat* flat, i32 parent, u32 child_index);

u32 tree_flat_parent_row(const TreeFlat* flat, u32 row);

u32 tree_flat_subtree_end(const TreeFlat* flat, u32 row);
