#include "unit_tests_common.h"
#include "ui/tree_flat.h"

#define FLAT_BIG_COUNT 1000000u

static const FlTreeExpanded s_flat_nodes[] = {
    { .parent = -1, .child_index = 0, .child_count = 2 },
    { .parent = 0, .child_index = 1, .child_count = 3 },
    { .parent = -1, .child_index = 2, .child_count = FLAT_BIG_COUNT },
};

typedef struct FlatExpect {
    u32 row;
    i32 parent;
    u32 child_index;
    u16 depth;
    i32 node;
} FlatExpect;

static bool flat_row_matches(const TreeFlat* flat, FlatExpect expect) {
    FlTreeVirtualRow row = tree_flat_row(flat, expect.row);
    bool same_place = row.parent == expect.parent && row.child_index == expect.child_index;
    return same_place && row.depth == expect.depth && row.node == expect.node;
}

UTEST(tree_flat, rows_follow_display_order) {
    FlArena* arena = arena_new(.reserved_size = MB(1));
    TreeFlat flat = tree_flat_build(arena, 3, s_flat_nodes, sizeof_array(s_flat_nodes));

    ASSERT_EQ_MSG(3u + 2u + 3u + FLAT_BIG_COUNT, flat.row_count, "rows are the root rows plus every shown child");
    const FlatExpect expect[] = {
        { 0, -1, 0, 0, 0 }, { 1, 0, 0, 1, -1 }, { 2, 0, 1, 1, 1 },
        { 3, 1, 0, 2, -1 }, { 5, 1, 2, 2, -1 }, { 6, -1, 1, 0, -1 },
        { 7, -1, 2, 0, 2 }, { 8, 2, 0, 1, -1 }, { 7 + FLAT_BIG_COUNT, 2, FLAT_BIG_COUNT - 1, 1, -1 },
    };
    for_count(i, sizeof_array(expect)) {
        ASSERT_TRUE_MSG(flat_row_matches(&flat, expect[i]), "a row reports its parent, child index, depth and node");
    }
    fl_arena_destroy(arena);
}

UTEST(tree_flat, cost_follows_expanded_nodes) {
    FlArena* arena = arena_new(.reserved_size = MB(1));
    TreeFlat flat = tree_flat_build(arena, 3, s_flat_nodes, sizeof_array(s_flat_nodes));

    ASSERT_TRUE_MSG(flat.range_count <= 2 * sizeof_array(s_flat_nodes) + 1,
                    "a million children are one range, not a million entries");
    fl_arena_destroy(arena);
}

UTEST(tree_flat, a_child_maps_back_to_its_row) {
    FlArena* arena = arena_new(.reserved_size = MB(1));
    TreeFlat flat = tree_flat_build(arena, 3, s_flat_nodes, sizeof_array(s_flat_nodes));

    ASSERT_EQ_MSG(0, tree_flat_row_of(&flat, -1, 0), "a root row before the expanded children");
    ASSERT_EQ_MSG(6, tree_flat_row_of(&flat, -1, 1), "a root row after an expanded subtree");
    ASSERT_EQ_MSG(4, tree_flat_row_of(&flat, 1, 1), "a nested child");
    ASSERT_EQ_MSG((i32)(8 + 500000), tree_flat_row_of(&flat, 2, 500000), "a child deep in a large node");
    ASSERT_EQ_MSG(-1, tree_flat_row_of(&flat, 1, 3), "a child index past the node's count has no row");
    fl_arena_destroy(arena);
}

UTEST(tree_flat, parent_row_and_subtree_end) {
    FlArena* arena = arena_new(.reserved_size = MB(1));
    TreeFlat flat = tree_flat_build(arena, 3, s_flat_nodes, sizeof_array(s_flat_nodes));

    ASSERT_EQ_MSG(0u, tree_flat_parent_row(&flat, 0), "a root row is its own parent row");
    ASSERT_EQ_MSG(0u, tree_flat_parent_row(&flat, 1), "a child's parent is the expanded row above it");
    ASSERT_EQ_MSG(2u, tree_flat_parent_row(&flat, 5), "a nested child's parent");
    ASSERT_EQ_MSG(7u, tree_flat_parent_row(&flat, 8 + 500000), "a child deep in a large node");

    ASSERT_EQ_MSG(6u, tree_flat_subtree_end(&flat, 0), "an expanded row's subtree covers its nested nodes");
    ASSERT_EQ_MSG(2u, tree_flat_subtree_end(&flat, 1), "a row that is not expanded has no subtree");
    ASSERT_EQ_MSG(6u, tree_flat_subtree_end(&flat, 2), "a nested expanded row ends where its parent does");
    ASSERT_EQ_MSG(6u, tree_flat_subtree_end(&flat, 5), "the last child of a node has no subtree");
    ASSERT_EQ_MSG(7u, tree_flat_subtree_end(&flat, 6), "a plain root row");
    ASSERT_EQ_MSG(flat.row_count, tree_flat_subtree_end(&flat, 7), "the last expanded row runs to the end");
    fl_arena_destroy(arena);
}

UTEST(tree_flat, no_expanded_nodes_is_a_flat_list) {
    FlArena* arena = arena_new(.reserved_size = MB(1));
    TreeFlat flat = tree_flat_build(arena, 5, nullptr, 7);

    ASSERT_EQ_MSG(5u, flat.row_count, "only the root rows");
    ASSERT_TRUE_MSG(flat_row_matches(&flat, (FlatExpect) { 4, -1, 4, 0, -1 }), "the last root row");

    TreeFlat empty = tree_flat_build(arena, 0, nullptr, 0);
    ASSERT_EQ_MSG(0u, empty.row_count, "an empty tree has no rows");
    fl_arena_destroy(arena);
}

UTEST(tree_flat, an_expanded_node_can_have_no_children) {
    const FlTreeExpanded nodes[] = {
        { .parent = -1, .child_index = 1, .child_count = 0 },
    };
    FlArena* arena = arena_new(.reserved_size = MB(1));
    TreeFlat flat = tree_flat_build(arena, 3, nodes, sizeof_array(nodes));

    ASSERT_EQ_MSG(3u, flat.row_count, "no rows are added");
    ASSERT_TRUE_MSG(flat_row_matches(&flat, (FlatExpect) { 1, -1, 1, 0, 0 }), "the row still reports its node");
    ASSERT_TRUE_MSG(flat_row_matches(&flat, (FlatExpect) { 2, -1, 2, 0, -1 }), "the next row is a plain root row");
    ASSERT_EQ_MSG(2u, tree_flat_subtree_end(&flat, 1), "its subtree is empty");
    fl_arena_destroy(arena);
}

#ifdef NDEBUG
UTEST(tree_flat, nodes_that_do_not_fit_are_left_out) {
    const FlTreeExpanded nodes[] = {
        { .parent = -1, .child_index = 1, .child_count = 2 }, { .parent = -1, .child_index = 0, .child_count = 4 },
        { .parent = 1, .child_index = 0, .child_count = 8 },  { .parent = 0, .child_index = 5, .child_count = 16 },
        { .parent = 7, .child_index = 0, .child_count = 32 },
    };
    FlArena* arena = arena_new(.reserved_size = MB(1));
    TreeFlat flat = tree_flat_build(arena, 3, nodes, sizeof_array(nodes));

    ASSERT_EQ_MSG(5u, flat.row_count, "only the first node is in order and inside its parent");
    fl_arena_destroy(arena);
}
#endif
