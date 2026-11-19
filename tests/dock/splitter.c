// Dock splitter: the ratio math and the live divider drag.

#include "dock/common.h"

UTEST(dock_splitter, ratio_math_and_clamp) {
    const f32 extent = 800.0f;

    ASSERT_NEAR(dock_splitter_ratio_at(200.0f, extent), 0.25f, 0.0001f);
    ASSERT_NEAR(dock_splitter_ratio_at(480.0f, extent), 0.60f, 0.0001f);

    // Both ends clamp to the min-size floor, so neither child can be dragged to zero.
    ASSERT_NEAR(dock_splitter_ratio_at(0.0f, extent), FL_DOCK_MIN_RATIO, 0.0001f);
    ASSERT_NEAR(dock_splitter_ratio_at(-50.0f, extent), FL_DOCK_MIN_RATIO, 0.0001f);
    ASSERT_NEAR(dock_splitter_ratio_at(extent, extent), 1.0f - FL_DOCK_MIN_RATIO, 0.0001f);
    ASSERT_NEAR(dock_splitter_ratio_at(extent + 50.0f, extent), 1.0f - FL_DOCK_MIN_RATIO, 0.0001f);

    // A zero extent, before any geometry exists, falls back to a clamped centre and avoids dividing by zero.
    ASSERT_NEAR(dock_splitter_ratio_at(0.0f, 0.0f), 0.5f, 0.0001f);
}

// The ratio changes only through the splitter-resize op: dock_splitter applies a SplitterResize request and
// nothing in the path writes geometry directly.
static void dock_splitter_drag_frame(void* user_data) {
    // Two single-panel leaves, so the only chrome is the divider between them.
    FlDockWorkspace* ws
        = dock_fixture_emit((DockFixture*)user_data, dock_split(FlDockAxis_X, 0.5f, dock_leaf(1), dock_leaf(2)));
    dock_workspace_end(ws);
}

UTEST(dock_splitter, divider_drag_resizes_through_op) {
    DOCK_APP(app, "dockdrag", 800, 600);
    DockFixture st = { 0 };

    DOCK_RUN_FRAMES(app, st, st, dock_splitter_drag_frame, 1);
    FlDockTree* tree = st.ws->tree;
    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Split, "root must be the X split");
    FlDockNodeId split_id = tree->root->id;
    ASSERT_NEAR(tree->root->split.ratio, 0.5f, 0.01f);

    // Find the divider element (name "fl_dock_divider", id = split node id, see dock_splitter.c).
    u32 divider_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("fl_dock_divider"), .id = (u32)split_id });
    ItemState* divider = hashmap_get(&st.ctx->shared->item_state, divider_id);
    ASSERT_TRUE_MSG(divider != nullptr && divider->state.size.x > 0.0f, "divider was not laid out");
    i32 cx = (i32)(divider->state.pos.x + divider->state.size.x * 0.5f);
    i32 cy = (i32)(divider->state.pos.y + divider->state.size.y * 0.5f);

    dock_press_at(DOCK_RIG(app, &st, dock_splitter_drag_frame), cx, cy);

    // Drag to x = 200, 25% of the 800px split. That is past the 4px threshold, so the drag is live and the ratio
    // follows the cursor's fraction of the split extent.
    dock_move_and_tick(DOCK_RIG(app, &st, dock_splitter_drag_frame), 200, cy, 1);
    ASSERT_NEAR_MSG(tree->root->split.ratio, 0.25f, 0.02f, "divider drag must set the ratio to the cursor fraction");

    // Past the edge the drag stops at the pixel floor, 120px of an 800px split, before the fractional one.
    dock_move_and_tick(DOCK_RIG(app, &st, dock_splitter_drag_frame), -100, cy, 1);
    ASSERT_NEAR_MSG(tree->root->split.ratio, FL_DOCK_MIN_TILE_WIDTH / 800.0f, 0.005f,
                    "dragging past the near edge clamps to the minimum tile width");

    dock_move_and_tick(DOCK_RIG(app, &st, dock_splitter_drag_frame), 900, cy, 1);
    ASSERT_NEAR_MSG(tree->root->split.ratio, 1.0f - (FL_DOCK_MIN_TILE_WIDTH + FL_DOCK_DIVIDER_THICKNESS) / 800.0f,
                    0.005f, "dragging past the far edge leaves the far child its minimum tile width");
}
