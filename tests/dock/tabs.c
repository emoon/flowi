// Dock tab strip: titles, widths, cursors and hidden panels.

#include "dock/common.h"

// Panel titles. Panels are declared after the emit, and the tab strip reads the title the workspace retained.

typedef struct DockTitleState {
    DockFixture fx;
    bool declare;         // off for the reserved frame, where the tabs keep their labels with nothing declared
    FlCursorShape cursor; // the shape the frame resolved, read inside it because the request is per-frame state
} DockTitleState;

// Longer than any tab can be, so it is elided at the maximum tab width.
#define DOCK_TITLE_LONG FL_S("A panel title far too long to ever fit inside one dock tab")

static void dock_title_frame(void* user_data) {
    DockTitleState* st = (DockTitleState*)user_data;
    // A root leaf of three panels. 200, the first listed, is selected at first.
    FlDockWorkspace* ws = dock_fixture_emit(&st->fx, dock_leaf(200, 201, 202));
    if (st->declare) {
        dock_workspace_declare(ws, 200, FL_S("Files"));
        dock_workspace_declare(ws, 201, FL_S("")); // untitled, so a blank tab and not "201"
        dock_workspace_declare(ws, 202, DOCK_TITLE_LONG);
    }
    dock_workspace_end(ws);
    st->cursor = (FlCursorShape)fl_cursor_get_requested().shape; // per-frame state, valid only inside the frame
}

// The laid-out state of the root leaf's index-th tab.
static ItemState* dock_tab_item(const DockTitleState* st, u32 index) {
    FlDockNodeId leaf_id = st->fx.ws->tree->root->id;
    return hashmap_get(&st->fx.ctx->shared->item_state, dock_tab_clay_id(leaf_id, index));
}

// Whether cmd is a text run ending in the ellipsis the elide pass appends, U+2026.
static bool dock_text_ends_in_ellipsis(const UiRenderCommand* cmd) {
    static const char ellipsis[3] = { (char)0xE2, (char)0x80, (char)0xA6 };
    const u64 len = cmd->text.length;
    if (cmd->type != UiRenderCmd_Text || cmd->text.data == nullptr) {
        return false;
    }
    return len > 3 && memcmp((const char*)cmd->text.data + (len - 3), ellipsis, 3) == 0;
}

// Matching the prefix as well as the ellipsis tells an elided label from one that vanished.
static bool frame_has_elided_text(FlString full, UiRenderCommand* out_cmd) {
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || !dock_text_ends_in_ellipsis(&cmd)) {
            continue;
        }
        const u64 prefix_len = cmd.text.length - 3;
        if (prefix_len < full.length && memcmp(cmd.text.data, full.data, prefix_len) == 0) {
            *out_cmd = cmd;
            return true;
        }
    }
    return false;
}

// A text the frame has to draw, or not draw.
typedef struct TextWant {
    FlString text;
    bool shown;
    const char* msg;
} TextWant;

static void dock_expect_texts(int* utest_result, const TextWant* rows, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        ASSERT_EQ_MSG((int)frame_has_text(rows[i].text), (int)rows[i].shown, rows[i].msg);
    }
}

UTEST(dock_workspace, tab_labels_render_retained_titles) {
    DOCK_APP(app, "docktitles", 800, 400);
    DockTitleState st = { .declare = true };

    // Frame 1 emits before any declaration has run, so it has no titles yet. Frame 2 is the steady state.
    DOCK_RUN_FRAMES(app, st, st.fx, dock_title_frame, 2);

    // The over-long title is also checked for being elided directly below, because the full string would also be
    // absent if the label had vanished.
    const TextWant steady[] = {
        { FL_S("Files"), true, "a titled tab renders its title" },
        { FL_S("200"), false, "the numeric panel id must never reach the screen" },
        { FL_S("201"), false, "an untitled panel renders a blank tab, not its id" },
        { FL_S("202"), false, "the numeric panel id must never reach the screen" },
        { DOCK_TITLE_LONG, false, "an over-long title must be elided, not rendered whole" },
    };
    dock_expect_texts(utest_result, steady, sizeof_array(steady));
    UiRenderCommand elided = { 0 };
    ASSERT_TRUE_MSG(frame_has_elided_text(DOCK_TITLE_LONG, &elided),
                    "the over-long title must render as a prefix of itself plus an ellipsis");
    ASSERT_TRUE_MSG(elided.width <= DOCK_TAB_MAX_WIDTH + 0.5f, "the elided run fits inside the max tab width");

    st.declare = false;
    fl_app_tick(app, &st, dock_title_frame);
    ASSERT_TRUE_MSG(dock_workspace_panel_reserved(st.fx.ws, 200), "200 must be reserved with nothing declared");
    const TextWant reserved[] = {
        { FL_S("Files"), true, "a reserved panel's tab keeps the title from its last declaration" },
        { FL_S("200"), false, "a reserved tab must not fall back to the numeric id either" },
    };
    dock_expect_texts(utest_result, reserved, sizeof_array(reserved));
}

UTEST(dock_workspace, blank_and_long_tabs_clamp_to_the_width_band) {
    DOCK_APP(app, "dockwidth", 800, 400);
    DockTitleState st = { .declare = true };
    DOCK_RUN_FRAMES(app, st, st.fx, dock_title_frame, 2);

    ItemState* blank = dock_tab_item(&st, 1);
    ItemState* long_tab = dock_tab_item(&st, 2);
    ASSERT_TRUE_MSG(blank != nullptr && long_tab != nullptr, "the blank and long tabs must both be laid out");
    ASSERT_NEAR_MSG((double)blank->state.size.x, (double)DOCK_TAB_MIN_WIDTH, 0.51,
                    "a blank tab keeps the minimum width so it stays hittable and draggable");
    ASSERT_NEAR_MSG((double)long_tab->state.size.x, (double)DOCK_TAB_MAX_WIDTH, 0.51,
                    "an over-long title caps at the maximum width and elides instead of growing the tab");

    // The min width is also a hit area: pressing the blank tab selects its panel.
    i32 cx = (i32)(blank->state.pos.x + blank->state.size.x * 0.5f);
    i32 cy = (i32)(blank->state.pos.y + blank->state.size.y * 0.5f);
    fl_app_inject_mouse_move(app, cx, cy);
    dock_settle_hover(app, &st, dock_title_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, dock_title_frame);
    ASSERT_TRUE_MSG(dock_workspace_panel_should_emit(st.fx.ws, 201), "clicking the blank tab must select its panel");
}

// Every tab reports the hand cursor on hover, the selected one included, because in a dock header the press that
// selects also begins the drag that pulls the panel out. The strip's empty remainder requests nothing.
UTEST(dock_workspace, every_tab_hovers_the_hand_cursor) {
    DOCK_APP(app, "dockcursor", 800, 400);
    DockTitleState st = { .declare = true };
    DOCK_RUN_FRAMES(app, st, st.fx, dock_title_frame, 2);

    // Tab 0 is the selected tab, 1 and 2 are not, and all three report the same shape.
    for (u32 i = 0; i < 3; i++) {
        ItemState* tab = dock_tab_item(&st, i);
        ASSERT_TRUE_MSG(tab != nullptr && tab->state.size.x > 0.0f, "every dock tab must be laid out");
        fl_app_inject_mouse_move(app, (i32)(tab->state.pos.x + tab->state.size.x * 0.5f),
                                 (i32)(tab->state.pos.y + tab->state.size.y * 0.5f));
        dock_settle_hover(app, &st, dock_title_frame);
        ASSERT_EQ_MSG((int)st.cursor, (int)FlCursorShape_Hand,
                      "hovering any dock tab, selected or not, must request the hand cursor");
    }

    // The strip's remainder, in the header row past the last tab, is inert and keeps the default arrow.
    ItemState* last = dock_tab_item(&st, 2);
    fl_app_inject_mouse_move(app, (i32)(last->state.pos.x + last->state.size.x + 40.0f),
                             (i32)(last->state.pos.y + last->state.size.y * 0.5f));
    dock_settle_hover(app, &st, dock_title_frame);
    ASSERT_EQ_MSG((int)st.cursor, (int)FlCursorShape_Arrow,
                  "the tab strip's empty remainder stays inert — no grab, so no affordance");
}

// A root split of two leaves, with all three panels declared every frame. Hiding a panel does not change how the
// app declares it.
static void dock_visibility_frame(void* user_data) {
    DockFixture* fx = (DockFixture*)user_data;
    FlDockWorkspace* ws = dock_fixture_emit(fx, dock_split(FlDockAxis_X, 0.5f, dock_leaf(300, 301), dock_leaf(302)));
    dock_workspace_declare(ws, 300, FL_S("Alpha"));
    dock_workspace_declare(ws, 301, FL_S("Beta"));
    dock_workspace_declare(ws, 302, FL_S("Gamma"));
    dock_workspace_end(ws);
}

UTEST(dock_workspace, hidden_panels_drop_their_tabs_then_the_leaf_out) {
    DOCK_APP(app, "dockvis", 800, 400);
    DockFixture fx = { 0 };
    DOCK_RUN_FRAMES(app, fx, fx, dock_visibility_frame, 2); // frame 1 installs the tree, frame 2 is the steady state

    FlDockTree* tree = fx.ws->tree;
    FlDockNode* left = dock_tree_find_leaf_with_panel(tree, 300);
    FlDockNode* right = dock_tree_find_leaf_with_panel(tree, 302);
    ASSERT_TRUE(left != nullptr && right != nullptr);
    const f32 left_w = dock_node_width(left->id); // showing the panels again has to restore this width
    const f32 right_w = dock_node_width(right->id);
    ASSERT_TRUE_MSG(left_w > 0.0f && right_w > 0.0f, "both tiles must be laid out to start with");
    ASSERT_TRUE(frame_has_text(FL_S("Beta")));

    // Hiding one tab of a two-tab leaf takes its tab off the strip and nothing else.
    dock_workspace_set_panel_visible(fx.ws, 301, false);
    fl_app_tick(app, &fx, dock_visibility_frame);
    const TextWant one_hidden[] = {
        { FL_S("Beta"), false, "a hidden panel renders no tab" },
        { FL_S("Alpha"), true, "its leaf keeps rendering the tabs that are shown" },
    };
    dock_expect_texts(utest_result, one_hidden, sizeof_array(one_hidden));
    // The hidden panel keeps its slot and its tab index.
    dock_expect_tab_order(utest_result, left, &(DockLeafWant) { .count = 2, .panels = { 300, 301 } });
    ASSERT_FALSE_MSG(dock_workspace_panel_reserved(fx.ws, 301), "a declared panel stays unreserved while hidden");
    ASSERT_NEAR_MSG(dock_node_width(left->id), left_w, 0.5f, "hiding one tab does not resize its leaf");

    // Hiding the rest of the leaf drops it from the displayed layout. Its space goes to its neighbour on the next
    // frame, and the logical split is untouched.
    dock_workspace_set_panel_visible(fx.ws, 300, false);
    dock_ticks(app, &fx, dock_visibility_frame, 2); // read-back is one frame behind
    const TextWant leaf_hidden[] = { { FL_S("Alpha"), false, "an all-hidden leaf renders no tabs at all" } };
    dock_expect_texts(utest_result, leaf_hidden, sizeof_array(leaf_hidden));
    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Split, "the logical split survives the hide");
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    ASSERT_NEAR_MSG(dock_node_width(right->id), dock_node_width(tree->root->id), 1.0f,
                    "the all-hidden leaf's space ends up entirely with its neighbour");

    // With everything hidden nothing is tiled: the last visible leaf has no neighbour to give its space to.
    dock_workspace_set_panel_visible(fx.ws, 302, false);
    fl_app_tick(app, &fx, dock_visibility_frame);
    const TextWant all_hidden[] = { { FL_S("Gamma"), false, "an all-hidden workspace renders no tabs at all" } };
    dock_expect_texts(utest_result, all_hidden, sizeof_array(all_hidden));
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    // Showing the panels again restores the earlier arrangement, since the tree never changed.
    dock_workspace_set_panel_visible(fx.ws, 300, true);
    dock_workspace_set_panel_visible(fx.ws, 301, true);
    dock_workspace_set_panel_visible(fx.ws, 302, true);
    dock_ticks(app, &fx, dock_visibility_frame, 2); // read-back is one frame behind
    ASSERT_NEAR_MSG(dock_node_width(left->id), left_w, 0.5f, "the re-shown leaf is back at its old width");
    ASSERT_NEAR_MSG(dock_node_width(right->id), right_w, 0.5f, "and so is its neighbour");
    const TextWant restored[] = {
        { FL_S("Alpha"), true, "the first tab is back on the strip" },
        { FL_S("Beta"), true, "and so is the second" },
    };
    dock_expect_texts(utest_result, restored, sizeof_array(restored));
    ASSERT_EQ_MSG((int)left->leaf.selected, 0, "and the leaf's selection was never rewritten");
    ASSERT_TRUE(dock_tree_check_invariants(tree));
}
