// Dock overlays and input: overlays opened from a panel body and clipped hit testing.

#include "dock/common.h"
#include <float.h>

// An overlay opened from a panel body draws past the panel's tile, up to the window. Both attach modes are
// covered: root-attached (a dialog or context menu) and element-attached (anchored to a widget in the body).
#define DOCK_SEAM_OVERLAY_W 250.0f
#define DOCK_SEAM_ID(lit) ((FlLayoutId) { .name = FL_S(lit), .id = 100u })
#define DOCK_SEAM_SCISSOR_MAX 16

static void dock_seam_frame(void* user_data) {
    // Panel 100 in the left half and 101 in the right. The overlays have to cross the midpoint between them.
    FlDockWorkspace* ws
        = dock_fixture_emit((DockFixture*)user_data, dock_split(FlDockAxis_X, 0.5f, dock_leaf(100), dock_leaf(101)));
    dock_panel(100, FL_S("seam")) {
        // Coloured so it emits a rectangle of its own. It is a plain, non-floating child of the body, so it
        // shows that the tile clip is in force around the overlays below.
        ui_layout_area_id(DOCK_SEAM_ID("seam_anchor"), .sizing = { sizing_grow(), sizing_fixed(40.0f) },
                          .color = ((FlColor) { 0, 0, 255, 255 })) {}

        fl_floating_area_id(DOCK_SEAM_ID("seam_root_overlay"), .attach_to = FlFloatingAttachTo_Root,
                            .pointer_capture = FlFloatingPointerCapture_Passthrough, .offset = { 200.0f, 150.0f },
                            .sizing = { sizing_fixed(DOCK_SEAM_OVERLAY_W), sizing_fixed(40.0f) }) {
            ui_layout_area_id(DOCK_SEAM_ID("seam_root_fill"), .sizing = { sizing_grow(), sizing_grow() },
                              .color = ((FlColor) { 255, 0, 0, 255 })) {}
        }

        // Anchored to the body's own element, starting at its horizontal centre so the fixed width runs past the
        // tile's right edge whatever the host size is.
        fl_floating_area_id(DOCK_SEAM_ID("seam_elem_overlay"), .attach_to = FlFloatingAttachTo_Element,
                            .parent_id = ui_layout_id_to_clay_id(DOCK_SEAM_ID("seam_anchor")),
                            .pointer_capture = FlFloatingPointerCapture_Passthrough,
                            .element_attach = FlFloatingAttachPoint_LeftTop,
                            .parent_attach = FlFloatingAttachPoint_CenterTop,
                            .sizing = { sizing_fixed(DOCK_SEAM_OVERLAY_W), sizing_fixed(40.0f) }) {
            ui_layout_area_id(DOCK_SEAM_ID("seam_elem_fill"), .sizing = { sizing_grow(), sizing_grow() },
                              .color = ((FlColor) { 0, 255, 0, 255 })) {}
        }
    }
    dock_workspace_end(ws);
}

// The right edges of the scissors open at one point of the draw list. Scissors nest, so each recorded edge is the
// intersection with the one below it.
typedef struct DockScissorStack {
    f32 right[DOCK_SEAM_SCISSOR_MAX];
    u32 depth; // counts every open scissor, of which the first DOCK_SEAM_SCISSOR_MAX are recorded
} DockScissorStack;

static void dock_scissor_push(DockScissorStack* stack, f32 right) {
    const bool nested = stack->depth > 0 && stack->depth <= DOCK_SEAM_SCISSOR_MAX;
    const f32 edge = nested ? fminf(right, stack->right[stack->depth - 1]) : right;
    if (stack->depth < DOCK_SEAM_SCISSOR_MAX) {
        stack->right[stack->depth] = edge;
    }
    stack->depth++;
}

static void dock_scissor_pop(DockScissorStack* stack) {
    if (stack->depth > 0) {
        stack->depth--;
    }
}

// The tightest right edge in force: FLT_MAX when none is open, and 0 when the stack is nested deeper than it
// records, so a caller fails and does not pass on an edge that was never tracked.
static f32 dock_scissor_clip_right(const DockScissorStack* stack) {
    if (stack->depth == 0) {
        return FLT_MAX;
    }
    return stack->depth <= DOCK_SEAM_SCISSOR_MAX ? stack->right[stack->depth - 1] : 0.0f;
}

// The rectangle the given element drew this frame, plus the right edge of the tightest scissor in force at that
// point (FLT_MAX when none). A command's bounding box is the same whether or not a scissor cuts it, so the clip
// edge is what shows whether the overlay escaped the panel.
static bool dock_seam_drawn_rect(FlLayoutId id, UiRenderCommand* out, f32* clip_right) {
    const u32 want = ui_layout_id_to_clay_id(id);
    DockScissorStack stack = { .depth = 0 };
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; ++i) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd)) {
            continue;
        }
        if (cmd.type == UiRenderCmd_ScissorStart) {
            dock_scissor_push(&stack, cmd.x + cmd.width);
        } else if (cmd.type == UiRenderCmd_ScissorEnd) {
            dock_scissor_pop(&stack);
        } else if (cmd.id == want && cmd.type == UiRenderCmd_Rectangle) {
            *out = cmd;
            *clip_right = dock_scissor_clip_right(&stack);
            return true;
        }
    }
    return false;
}

// The right edge of panel 100's tile, which every overlay below has to cross.
static f32 dock_seam_tile_right(FlDockWorkspace* ws) {
    FlDockNode* leaf = dock_tree_find_leaf_with_panel(ws->tree, 100);
    if (leaf == nullptr) {
        return -1.0f;
    }
    Clay_ElementData tile = dock_element(dock_layout_content_element_id(leaf->id));
    return tile.found ? tile.boundingBox.x + tile.boundingBox.width : -1.0f;
}

// Checks the overlay fill named by fill_id draws from inside panel 100's tile to past its right edge with no scissor
// cutting it back, while the non-floating anchor is clipped to that tile.
static void dock_seam_expect_overlay_unclipped(int* utest_result, FlDockWorkspace* ws, FlLayoutId fill_id) {
    f32 seam = dock_seam_tile_right(ws);
    ASSERT_GT_MSG(seam, 0.0f, "panel 100's tile must lay out");

    UiRenderCommand anchor = { 0 };
    f32 anchor_clip = 0.0f;
    ASSERT_TRUE_MSG(dock_seam_drawn_rect(DOCK_SEAM_ID("seam_anchor"), &anchor, &anchor_clip), "the anchor drew");
    ASSERT_LE_MSG(anchor_clip, seam + 0.5f, "control: a non-floating child of the panel body IS clipped to the tile");

    UiRenderCommand drawn = { 0 };
    f32 clip_right = 0.0f;
    ASSERT_TRUE_MSG(dock_seam_drawn_rect(fill_id, &drawn, &clip_right), "an overlay opened inside a panel body draws");
    ASSERT_LT_MSG(drawn.x, seam, "the overlay starts inside the panel that opened it");
    ASSERT_GT_MSG(drawn.x + drawn.width, seam, "and it draws on past that panel's tile, across the dock seam");
    ASSERT_GE_MSG(clip_right, drawn.x + drawn.width,
                  "no scissor in force may cut the overlay back to the tile it was opened from");
}

UTEST(dock_workspace, root_attached_overlay_from_a_panel_crosses_the_dock_seam) {
    DOCK_APP(app, "dockseam", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_seam_frame, 2); // frame 1 installs the tree, frame 2 lays it out settled
    dock_seam_expect_overlay_unclipped(utest_result, st.ws, DOCK_SEAM_ID("seam_root_fill"));
}

UTEST(dock_workspace, element_attached_overlay_escapes_the_panel_clip) {
    DOCK_APP(app, "dockseam2", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_seam_frame, 2);
    dock_seam_expect_overlay_unclipped(utest_result, st.ws, DOCK_SEAM_ID("seam_elem_fill"));
}

// A panel body that overflows its tile is clipped to it, and so is its input. The nested column tests the whole
// clip chain and not only the nearest clip ancestor: a scroll area inside a panel has a clip box taller than its
// tile.

#define DOCK_HIT_ID(lit) ((FlLayoutId) { .name = FL_S(lit), .id = 100u })
#define DOCK_HIT_COL_W 120.0f
#define DOCK_HIT_COL_H 600.0f

static void dock_hit_frame(void* user_data) {
    // Panel 100 on top and 101 below it. The overflow must not take input across the edge between them.
    FlDockWorkspace* ws
        = dock_fixture_emit((DockFixture*)user_data, dock_split(FlDockAxis_Y, 0.5f, dock_leaf(100), dock_leaf(101)));
    dock_panel(100, FL_S("hit")) {
        // Clipped by the panel tile alone.
        ui_layout_area_id(
            DOCK_HIT_ID("hit_direct"), .sizing = { sizing_fixed(DOCK_HIT_COL_W), sizing_fixed(DOCK_HIT_COL_H) },
            .color = ((FlColor) { 0, 0, 255, 255 }), .placed = { .enabled = true, .offset = { 0.0f, 0.0f } }) {
            ui_item_behavior(DOCK_HIT_ID("hit_direct"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
        }
        // Clipped by the panel tile and by this scroll area, whose own box runs past the tile.
        ui_layout_area_id(DOCK_HIT_ID("hit_scroll"),
                          .sizing = { sizing_fixed(DOCK_HIT_COL_W), sizing_fixed(DOCK_HIT_COL_H) },
                          .clip = { .horizontal = true, .vertical = true, .manual_scroll = true },
                          .placed = { .enabled = true, .offset = { 200.0f, 0.0f } }) {
            ui_layout_area_id(DOCK_HIT_ID("hit_nested"),
                              .sizing = { sizing_fixed(DOCK_HIT_COL_W), sizing_fixed(DOCK_HIT_COL_H) },
                              .color = ((FlColor) { 0, 255, 0, 255 })) {
                ui_item_behavior(DOCK_HIT_ID("hit_nested"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    dock_panel(101, FL_S("below")) {}
    dock_workspace_end(ws);
}

// The bottom edge of panel 100's tile, which the overflowing columns above must not take input across.

static f32 dock_hit_tile_bottom(FlDockWorkspace* ws) {
    FlDockNode* leaf = dock_tree_find_leaf_with_panel(ws->tree, 100);
    if (leaf == nullptr) {
        return -1.0f;
    }
    Clay_ElementData tile = dock_element(dock_layout_content_element_id(leaf->id));
    return tile.found ? tile.boundingBox.y + tile.boundingBox.height : -1.0f;
}

// Settles the pointer on a position and returns the hot id it resolved to. Hot is set at the end of a frame, so
// one tick is enough, but a second keeps the read clear of the frame that installed the position.

static u32 dock_hit_hot_at(FlApp* app, DockFixture* st, void (*frame)(void*), f32 x, f32 y) {
    fl_app_inject_mouse_move(app, (i32)x, (i32)y);
    dock_settle_hover(app, st, frame);
    return st->ctx->hot_id;
}

// The hot id after the pointer settles on the centre of an element.
static u32 dock_hit_hot_at_centre(FlApp* app, DockFixture* st, void (*frame)(void*), Clay_ElementData element) {
    return dock_hit_hot_at(app, st, frame, element.boundingBox.x + element.boundingBox.width * 0.5f,
                           element.boundingBox.y + element.boundingBox.height * 0.5f);
}

// Checks the element named id is hot inside panel 100's tile and not hot past the tile's bottom edge, though its own
// box reaches there.
static void dock_hit_expect_clipped_to_tile(int* utest_result, FlApp* app, DockFixture* st, FlLayoutId id) {
    const f32 tile_bottom = dock_hit_tile_bottom(st->ws);
    ASSERT_GT_MSG(tile_bottom, 0.0f, "panel 100's tile must lay out");

    const u32 clay_id = ui_layout_id_to_clay_id(id);
    Clay_ElementData element = dock_element(clay_id);
    ASSERT_TRUE_MSG(element.found, "the overflowing element must lay out");
    const f32 probe_y = tile_bottom + 20.0f;
    ASSERT_GT_MSG(element.boundingBox.y + element.boundingBox.height, probe_y,
                  "the element must reach past the tile for this to test anything");

    const f32 x = element.boundingBox.x + element.boundingBox.width * 0.5f;
    ASSERT_EQ_MSG(clay_id, dock_hit_hot_at(app, st, dock_hit_frame, x, (element.boundingBox.y + tile_bottom) * 0.5f),
                  "control: inside the tile the element is hot");
    ASSERT_NE_MSG(clay_id, dock_hit_hot_at(app, st, dock_hit_frame, x, probe_y),
                  "input below the tile must not reach the content that overflowed it");
}

UTEST(dock_workspace, panel_overflow_takes_no_input_past_the_tile) {
    DOCK_APP(app, "dockhit", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_hit_frame, 2); // frame 1 installs the tree, frame 2 lays it out settled
    dock_hit_expect_clipped_to_tile(utest_result, app, &st, DOCK_HIT_ID("hit_direct"));
}

UTEST(dock_workspace, a_nested_scroll_area_does_not_widen_the_tile_it_sits_in) {
    DOCK_APP(app, "dockhit2", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_hit_frame, 2);

    const f32 tile_bottom = dock_hit_tile_bottom(st.ws);
    ASSERT_GT_MSG(tile_bottom, 0.0f, "panel 100's tile must lay out");

    Clay_ElementData scroll = dock_layout_element(DOCK_HIT_ID("hit_scroll"));
    Clay_ElementData nested = dock_layout_element(DOCK_HIT_ID("hit_nested"));
    ASSERT_TRUE_MSG(scroll.found && nested.found, "the scroll area and its child must lay out");

    const f32 probe_y = tile_bottom + 20.0f;
    ASSERT_GT_MSG(scroll.boundingBox.y + scroll.boundingBox.height, probe_y,
                  "the probe must sit inside the scroll area's own clip box, past the tile — the nested case");
    ASSERT_GT_MSG(nested.boundingBox.y + nested.boundingBox.height, probe_y, "and inside the child's own rect");

    dock_hit_expect_clipped_to_tile(utest_result, app, &st, DOCK_HIT_ID("hit_nested"));
}

// An overlay opened from a panel body is a layout root of its own, under no scissor, so the clipped hit test
// leaves it hot everywhere it paints, including past the tile of the panel that opened it. Both attach modes are
// covered.

#define DOCK_OVL_ID(lit) ((FlLayoutId) { .name = FL_S(lit), .id = 100u })
#define DOCK_OVL_W 100.0f
#define DOCK_OVL_H 40.0f

static void dock_overlay_hit_frame(void* user_data) {
    // Panel 100 on top and 101 below it. The overlays have to stay hot across the edge between them.
    FlDockWorkspace* ws
        = dock_fixture_emit((DockFixture*)user_data, dock_split(FlDockAxis_Y, 0.5f, dock_leaf(100), dock_leaf(101)));
    dock_panel(100, FL_S("ovl")) {
        // Fills the tile, so its bottom edge is the tile's bottom edge.
        ui_layout_area_id(DOCK_OVL_ID("ovl_anchor"), .sizing = { sizing_grow(), sizing_grow() },
                          .color = ((FlColor) { 0, 0, 255, 255 })) {}

        fl_floating_area_id(DOCK_OVL_ID("ovl_root"), .attach_to = FlFloatingAttachTo_Root, .offset = { 400.0f, 300.0f },
                            .sizing = { sizing_fixed(DOCK_OVL_W), sizing_fixed(DOCK_OVL_H) }) {
            ui_layout_area_id(DOCK_OVL_ID("ovl_root_fill"), .sizing = { sizing_grow(), sizing_grow() },
                              .color = ((FlColor) { 255, 0, 0, 255 })) {
                ui_item_behavior(DOCK_OVL_ID("ovl_root_fill"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }

        // Hangs off the anchor's bottom edge, so it is entirely below the tile.
        fl_floating_area_id(DOCK_OVL_ID("ovl_elem"), .attach_to = FlFloatingAttachTo_Element,
                            .parent_id = ui_layout_id_to_clay_id(DOCK_OVL_ID("ovl_anchor")),
                            .element_attach = FlFloatingAttachPoint_LeftTop,
                            .parent_attach = FlFloatingAttachPoint_LeftBottom,
                            .sizing = { sizing_fixed(DOCK_OVL_W), sizing_fixed(DOCK_OVL_H) }) {
            ui_layout_area_id(DOCK_OVL_ID("ovl_elem_fill"), .sizing = { sizing_grow(), sizing_grow() },
                              .color = ((FlColor) { 0, 255, 0, 255 })) {
                ui_item_behavior(DOCK_OVL_ID("ovl_elem_fill"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    dock_panel(101, FL_S("below")) {}
    dock_workspace_end(ws);
}

UTEST(dock_workspace, an_overlay_stays_hot_past_the_tile_it_was_opened_from) {
    DOCK_APP(app, "dockovl", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_overlay_hit_frame, 2); // frame 1 installs the tree, frame 2 lays it out settled

    const f32 tile_bottom = dock_hit_tile_bottom(st.ws);
    ASSERT_GT_MSG(tile_bottom, 0.0f, "panel 100's tile must lay out");

    const u32 root_id = ui_layout_id_to_clay_id(DOCK_OVL_ID("ovl_root_fill"));
    Clay_ElementData root_fill = dock_element(root_id);
    ASSERT_TRUE_MSG(root_fill.found, "the root-attached overlay must lay out");
    ASSERT_GT_MSG(root_fill.boundingBox.y, tile_bottom,
                  "the root-attached overlay must sit past the tile for this to test anything");
    ASSERT_EQ_MSG(root_id, dock_hit_hot_at_centre(app, &st, dock_overlay_hit_frame, root_fill),
                  "a root-attached overlay is hot outside the tile of the panel that opened it");

    const u32 elem_id = ui_layout_id_to_clay_id(DOCK_OVL_ID("ovl_elem_fill"));
    Clay_ElementData elem_fill = dock_element(elem_id);
    ASSERT_TRUE_MSG(elem_fill.found, "the element-attached overlay must lay out");
    ASSERT_GE_MSG(elem_fill.boundingBox.y, tile_bottom - 0.5f,
                  "the element-attached overlay must hang off the tile's bottom edge");
    ASSERT_EQ_MSG(elem_id, dock_hit_hot_at_centre(app, &st, dock_overlay_hit_frame, elem_fill),
                  "an overlay anchored to a widget at the tile's bottom edge is hot below the tile");
}
