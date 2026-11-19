// Dock layout: the tree emitted to Clay and the node rects read back.

#include "dock/common.h"

// Dock layout: the logical tree emitted to Clay, and the node rects read back (ui/dock/dock_layout.{h,c}).

// One direct layout pass over tree into a w x h region: the dock_layout emit path with no workspace around it.
static void dock_layout_frame(FlUiCtx* ctx, FlDockTree* tree, f32 w, f32 h) {
    ctx->layout_width = w;
    ctx->layout_height = h;
    ctx->delta_time = 1.0f / 60.0f; // normally written by the app loop, which these frames do not run
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = w, .height = h });
    ui_update_begin(ctx);
    dock_layout_emit(tree, &dock_workspace_current()->panels, 0, nullptr);
    ui_update_end(ctx, 1.0f / 60.0f);
}

// A two-leaf X split at ratio.
static FlDockTree* dock_two_leaf_split(f32 ratio) {
    return dock_tree_build(dock_split(FlDockAxis_X, ratio, dock_leaf(1), dock_leaf(2)));
}

// A rect the layout read back and the rect it has to be.
typedef struct DockRectWant {
    const FlDockRect* got;
    FlDockRect want;
} DockRectWant;

static void dock_expect_rects(int* utest_result, const DockRectWant* wants, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        const f32 got[4] = { wants[i].got->x, wants[i].got->y, wants[i].got->width, wants[i].got->height };
        const f32 want[4] = { wants[i].want.x, wants[i].want.y, wants[i].want.width, wants[i].want.height };
        for (u32 k = 0; k < 4; ++k) {
            ASSERT_NEAR(got[k], want[k], 0.5f);
        }
    }
}

// Two values that must be the same edge of the tiling.
typedef struct DockFlushPair {
    f32 a;
    f32 b;
} DockFlushPair;

static void dock_expect_flush(int* utest_result, const DockFlushPair* pairs, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        ASSERT_NEAR_MSG(pairs[i].a, pairs[i].b, 0.5f, "dock rects must tile with no gap or overlap");
    }
}

// Asserts node is a split on axis.
static void dock_expect_split_axis(int* utest_result, const FlDockNode* node, FlDockAxis axis) {
    ASSERT_EQ((int)node->kind, (int)FlDockNodeKind_Split);
    ASSERT_EQ((int)node->split.axis, (int)axis);
}

// A dock tree of three leaves: leaf 1 on the left at ratio 0.25, and the right part split into leaves 2 on top and
// 3 below at ratio 0.5.
static FlDockTree* dock_three_leaf_tree(f32 left_ratio, f32 right_ratio) {
    return dock_tree_build(dock_split(FlDockAxis_X, left_ratio, dock_leaf(1),
                                      dock_split(FlDockAxis_Y, right_ratio, dock_leaf(2), dock_leaf(3))));
}

UTEST(dock_layout, tile_no_gaps_match_ratios) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    //   S = X-split (ratio 0.25)
    //     child0 = LA  leaf{1}              (left, 25% width)
    //     child1 = T = Y-split (ratio 0.50) (right, 75% width)
    //       child0 = LB leaf{2}            (top, 50% height)
    //       child1 = LC leaf{3}            (bottom, 50% height)
    FlDockTree* tree = dock_three_leaf_tree(0.25f, 0.5f);
    ASSERT_TRUE(tree != nullptr);
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    const FlDockNode* s = tree->root;
    dock_expect_split_axis(utest_result, s, FlDockAxis_X);
    const FlDockNode* t = s->split.child[1];
    dock_expect_split_axis(utest_result, t, FlDockAxis_Y);
    const FlDockNodeId ids[5] = { s->id, s->split.child[0]->id, t->id, t->split.child[0]->id, t->split.child[1]->id };

    // Rects are valid only after the layout pass (ui_update_end).
    const f32 w = 800.0f;
    const f32 h = 600.0f;
    dock_layout_frame(ctx, tree, w, h);

    FlDockLayout layout;
    dock_layout_read_rects(tree, tree->arena, &layout);
    ASSERT_EQ((int)layout.count, 5); // S, LA, T, LB, LC

    const FlDockRect* r_s = dock_layout_find_rect(&layout, ids[0]);
    const FlDockRect* r_la = dock_layout_find_rect(&layout, ids[1]);
    const FlDockRect* r_t = dock_layout_find_rect(&layout, ids[2]);
    const FlDockRect* r_lb = dock_layout_find_rect(&layout, ids[3]);
    const FlDockRect* r_lc = dock_layout_find_rect(&layout, ids[4]);
    ASSERT_TRUE_MSG(r_s && r_la && r_t && r_lb && r_lc, "every node rect must be read back after layout");

    // A divider strip of FL_DOCK_DIVIDER_THICKNESS fills each split's seam. child[0] keeps its exact percent of
    // the parent, the divider takes a fixed strip, and child[1] grows into the remainder. From the ratios: LA is
    // the left 25% (200px exact), then a divider, then T grows into the rest. Inside T (Y split, ratio 0.5): LB is
    // the top 50% (300px exact), then a divider, then LC grows into the rest.
    const f32 d = FL_DOCK_DIVIDER_THICKNESS;
    const DockRectWant rects[] = {
        { r_s, { 0.0f, 0.0f, w, h } },
        { r_la, { 0.0f, 0.0f, 200.0f, 600.0f } },
        { r_t, { 200.0f + d, 0.0f, 600.0f - d, 600.0f } },
        { r_lb, { 200.0f + d, 0.0f, 600.0f - d, 300.0f } },
        { r_lc, { 200.0f + d, 300.0f + d, 600.0f - d, 300.0f - d } },
    };
    dock_expect_rects(utest_result, rects, sizeof_array(rects));

    // The tiling as adjacency checks, independent of the absolute numbers above.
    const DockFlushPair flush[] = {
        { r_la->x + r_la->width + d, r_t->x },
        { r_la->x, r_s->x },
        { r_t->x + r_t->width, r_s->x + r_s->width },
        { r_la->height, r_s->height },
        { r_t->height, r_s->height },
        { r_lb->y + r_lb->height + d, r_lc->y },
        { r_lb->y, r_t->y },
        { r_lc->y + r_lc->height, r_t->y + r_t->height },
        { r_lb->x, r_t->x },
        { r_lb->width, r_t->width },
        { r_lc->x, r_t->x },
        { r_lc->width, r_t->width },
    };
    dock_expect_flush(utest_result, flush, sizeof_array(flush));

    const DockFlushPair shares[] = { { r_la->width / r_s->width, 0.25f }, { r_lb->height / r_t->height, 0.5f } };
    for (u32 i = 0; i < sizeof_array(shares); ++i) {
        ASSERT_NEAR(shares[i].a, shares[i].b, 0.01f);
    }

    dock_tree_destroy(tree);
}

// The minimum tile floor and the whole-pixel tile edge. Both are applied at emit, on the extent the previous
// frame's layout produced, and neither is written back into the tree.

// A clamp input and the ratio it has to produce.
typedef struct DockClampCase {
    f32 ratio;
    FlDockAxis axis;
    f32 want;
} DockClampCase;

// Every case clamps against extent and none of them is starved.
static void dock_expect_clamps(int* utest_result, const DockClampCase* cases, u32 count, f32 extent) {
    for (u32 i = 0; i < count; ++i) {
        bool starved = true;
        const f32 got = dock_layout_clamp_ratio(cases[i].ratio, cases[i].axis, extent, &starved);
        ASSERT_NEAR(got, cases[i].want, 0.0001f);
        ASSERT_FALSE(starved);
    }
}

// A ratio and the pixel its snapped far edge has to land on within an extent.
typedef struct DockSnapCase {
    f32 ratio;
    f32 extent;
    f32 want_px;
} DockSnapCase;

static void dock_expect_snaps(int* utest_result, const DockSnapCase* cases, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        ASSERT_NEAR(dock_layout_snap_ratio(cases[i].ratio, cases[i].extent) * cases[i].extent, cases[i].want_px,
                    0.001f);
    }
}

UTEST(dock_layout, tile_floor_and_pixel_snap_math) {
    const f32 e = 800.0f;
    const f32 d = FL_DOCK_DIVIDER_THICKNESS;

    // With room for both floors only the ends move, and a Y split applies the floor to the tile height.
    const DockClampCase roomy[] = {
        { 0.5f, FlDockAxis_X, 0.5f },
        { 0.01f, FlDockAxis_X, FL_DOCK_MIN_TILE_WIDTH / e },
        { 0.99f, FlDockAxis_X, 1.0f - (FL_DOCK_MIN_TILE_WIDTH + d) / e },
        { 0.0f, FlDockAxis_Y, FL_DOCK_MIN_TILE_HEIGHT / e },
    };
    dock_expect_clamps(utest_result, roomy, sizeof_array(roomy), e);

    // With less room than two floors plus the divider the clamp has no solution. Both children take half the
    // room, whatever the ratio asked for, and the split reports itself starved.
    const f32 tight = 2.0f * FL_DOCK_MIN_TILE_WIDTH + d - 20.0f;
    bool starved = false;
    const f32 first = dock_layout_clamp_ratio(0.2f, FlDockAxis_X, tight, &starved) * tight;
    ASSERT_TRUE_MSG(starved, "a split too small for both floors reports starved");
    ASSERT_NEAR_MSG(first, (tight - d) - first, 0.01f, "both children of a starved split degrade to the same size");
    ASSERT_NEAR(dock_layout_clamp_ratio(0.8f, FlDockAxis_X, tight, nullptr) * tight, first, 0.01f);

    // On the first frame nothing has been read back yet, and both do nothing.
    const DockClampCase unmeasured[] = { { 0.123f, FlDockAxis_X, 0.123f } };
    dock_expect_clamps(utest_result, unmeasured, sizeof_array(unmeasured), 0.0f);
    ASSERT_NEAR(dock_layout_snap_ratio(0.123f, 0.0f), 0.123f, 0.0001f);

    // The snap lands child[0]'s far edge on a whole pixel of the extent it was measured against.
    const DockSnapCase snaps[] = {
        { 0.5f, 801.0f, 401.0f }, // 400.5 rounds up
        { 0.3f, 801.0f, 240.0f }, // 240.3 rounds down
        { 1.0f / 3.0f, 800.0f, 267.0f },
    };
    dock_expect_snaps(utest_result, snaps, sizeof_array(snaps));
}

UTEST(dock_layout, shrink_then_re_expand_restores_the_ratios) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlDockTree* tree = dock_three_leaf_tree(0.25f, 0.5f);
    ASSERT_TRUE(tree != nullptr);

    FlDockNode* s = tree->root;
    FlDockNode* t = s->split.child[1];
    dock_layout_frame(ctx, tree, 800.0f, 600.0f);
    dock_layout_frame(ctx, tree, 800.0f, 600.0f);

    const f32 s_ratio = s->split.ratio;
    const f32 t_ratio = t->split.ratio;

    // Far below every floor, for long enough that any write-back would have had many frames to accumulate.
    for (int i = 0; i < 16; ++i) {
        dock_layout_frame(ctx, tree, 150.0f, 110.0f);
    }
    ASSERT_TRUE_MSG(dock_tree_check_invariants(tree), "starving the layout leaves the tree invariant-clean");

    FlDockLayout squeezed;
    dock_layout_read_rects(tree, tree->arena, &squeezed);
    for (u32 i = 0; i < squeezed.count; ++i) {
        ASSERT_TRUE_MSG(squeezed.nodes[i].found && squeezed.nodes[i].rect.width > 0.0f
                            && squeezed.nodes[i].rect.height > 0.0f,
                        "no node drops out of the layout below the floor — the workspace overflows and clips");
    }

    for (int i = 0; i < 16; ++i) {
        dock_layout_frame(ctx, tree, 800.0f, 600.0f);
    }

    ASSERT_NEAR_MSG(s->split.ratio, s_ratio, 1e-5f, "a resize is not a layout act: the ratios come back unchanged");
    ASSERT_NEAR_MSG(t->split.ratio, t_ratio, 1e-5f, "a resize is not a layout act: the ratios come back unchanged");

    dock_tree_destroy(tree);
}

UTEST(dock_layout, starved_split_degrades_symmetrically_and_reports_once) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // 0.05 of 800px is 40px, well under the floor, so the clamp has to lift it.
    FlDockTree* tree = dock_two_leaf_split(0.05f);
    ASSERT_TRUE(tree != nullptr);
    FlDockNode* s = tree->root;
    FlDockNodeId id_a = s->split.child[0]->id;
    FlDockNodeId id_b = s->split.child[1]->id;

    // Wide enough for both floors: the lopsided ratio is lifted to exactly the minimum tile width.
    for (int i = 0; i < 3; ++i) {
        dock_layout_frame(ctx, tree, 800.0f, 600.0f);
    }
    FlDockLayout wide;
    dock_layout_read_rects(tree, tree->arena, &wide);
    ASSERT_NEAR_MSG(dock_layout_find_rect(&wide, id_a)->width, FL_DOCK_MIN_TILE_WIDTH, 1.0f,
                    "a ratio under the floor is lifted to exactly the minimum tile width");
    ASSERT_FALSE(s->split.starve_logged);

    // Below two floors plus the divider neither child can have its floor, so they share what is left evenly.
    for (int i = 0; i < 3; ++i) {
        dock_layout_frame(ctx, tree, 200.0f, 600.0f);
    }
    FlDockLayout narrow;
    dock_layout_read_rects(tree, tree->arena, &narrow);
    const FlDockRect* a = dock_layout_find_rect(&narrow, id_a);
    const FlDockRect* b = dock_layout_find_rect(&narrow, id_b);
    ASSERT_TRUE_MSG(a != nullptr && b != nullptr, "both leaves keep a tile below the floor");
    ASSERT_NEAR_MSG(a->width, b->width, 1.0f, "both children of a starved split degrade symmetrically");
    ASSERT_TRUE_MSG(s->split.starve_logged, "the starved split latched its one error");

    // The starved flag latches: it is reported once, and it arms again once the room comes back.
    for (int i = 0; i < 3; ++i) {
        dock_layout_frame(ctx, tree, 800.0f, 600.0f);
    }
    ASSERT_FALSE_MSG(s->split.starve_logged, "room coming back clears the latch");

    dock_tree_destroy(tree);
}

// A tile edge and what has to be true of it.
typedef struct DockEdge {
    f32 px;
    const char* msg;
} DockEdge;

// Every edge of the tile lands on a whole pixel.
static void dock_expect_whole_pixel_edges(int* utest_result, const FlDockRect* r) {
    const DockEdge edges[] = {
        { r->x, "a tile's left edge lands on a whole pixel" },
        { r->y, "a tile's top edge lands on a whole pixel" },
        { r->x + r->width, "and so does its right edge" },
        { r->y + r->height, "and its bottom edge" },
    };
    for (u32 i = 0; i < sizeof_array(edges); ++i) {
        ASSERT_NEAR_MSG(edges[i].px, roundf(edges[i].px), 0.01f, edges[i].msg);
    }
}

UTEST(dock_layout, tile_edges_land_on_whole_pixels) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlDockTree* tree = dock_three_leaf_tree(FL_DOCK_SPLIT_RATIO_DEFAULT, FL_DOCK_SPLIT_RATIO_DEFAULT);
    ASSERT_TRUE(tree != nullptr);

    // Odd extents and half ratios: every edge wants to land between two pixels.
    const f32 w = 801.0f;
    const f32 h = 601.0f;
    // The extent read-back needs two frames: the first has no geometry at all, and the second only the outer
    // split's.
    for (int i = 0; i < 3; ++i) {
        dock_layout_frame(ctx, tree, w, h);
    }

    for (int frame = 0; frame < 10; ++frame) {
        FlDockLayout layout;
        dock_layout_read_rects(tree, tree->arena, &layout);
        for (u32 i = 0; i < layout.count; ++i) {
            ASSERT_TRUE_MSG(layout.nodes[i].found, "every node laid out");
            dock_expect_whole_pixel_edges(utest_result, &layout.nodes[i].rect);
        }
        dock_layout_frame(ctx, tree, w, h);
    }

    dock_tree_destroy(tree);
}

UTEST(dock_layout, a_collapse_hands_the_survivor_the_merged_rect) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // split(leaf{10} | leaf{20}) at 50/50. Closing one panel empties its leaf, which collapses. The survivor takes
    // the whole merged rect on the next frame.
    FlDockTree* tree = dock_pair_tree(10, 20);
    ASSERT_TRUE(tree != nullptr);

    FlDockRequest close = { .kind = FlDockRequest_Close, .panel = 10 };
    ASSERT_TRUE(dock_tree_apply(tree, &close));
    ASSERT_TRUE_MSG(dock_tree_check_invariants(tree), "the tree stays clean through the collapse");
    ASSERT_EQ((int)tree->root->kind, (int)FlDockNodeKind_Leaf);

    dock_layout_frame(ctx, tree, 800.0f, 600.0f);

    FlDockLayout layout;
    dock_layout_read_rects(tree, tree->arena, &layout);
    const FlDockRect* r = dock_layout_find_rect(&layout, tree->root->id);
    ASSERT_TRUE(r != nullptr);
    ASSERT_NEAR_MSG(r->width, 800.0f, 0.5f, "the survivor fills the merged rect immediately");
    ASSERT_NEAR(r->height, 600.0f, 0.5f);

    dock_tree_destroy(tree);
}
