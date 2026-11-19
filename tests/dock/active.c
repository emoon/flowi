// Dock active panel: activation by press and the keyboard-ring blur.

#include "dock/common.h"
#include "ui/ui_popup.h"

// The active panel: one panel id per workspace, moved by a press inside a tile, and the keyboard-ring blur that
// goes with every change of it (ui/dock/dock_workspace.{h,c}).

#define DOCK_ACTIVE_MODAL_SCOPE 0xD0C4A1u

// Two single-panel leaves side by side, each body holding one focusable widget. text_input swaps the right body's
// button for a text input, the one widget family that claims the keyboard ring on its own click.
typedef struct DockActiveState {
    DockFixture fx;
    bool text_input;
    bool modal;
    bool popup;
    bool skip_right; // stop declaring 501, so its slot reads as reserved
} DockActiveState;

// The text input's state. File-static because the UI context keeps a pointer to the focused input.
static FlTextInputState s_dock_active_input;

static void dock_active_frame(void* user_data) {
    DockActiveState* st = (DockActiveState*)user_data;
    FlUiCtx* ctx = g_ui_ctx;
    if (ctx != nullptr) {
        ctx->physical_keyboard_available = true; // so a focused text input opens no virtual keyboard
    }
    if (st->modal) {
        fl_modal_begin_frame(DOCK_ACTIVE_MODAL_SCOPE);
    } else if (ctx != nullptr && ctx->modal_scope_id != 0) {
        fl_modal_close();
    }

    FlDockWorkspace* ws = dock_fixture_emit(&st->fx, dock_split(FlDockAxis_X, 0.5f, dock_leaf(500), dock_leaf(501)));
    dock_panel(500, FL_S("Left")) {
        ui_button(FL_S("left_button"));
    }
    if (!st->skip_right) {
        dock_panel(501, FL_S("Right")) {
            if (st->text_input) {
                ui_text_input(FL_S("right_input"), &s_dock_active_input, (FlTextInputConfig) { 0 });
            } else {
                ui_button(FL_S("right_button"));
            }
        }
    }
    dock_workspace_end(ws);

    if (st->popup) {
        // Anchored to a point, because this frame emits no element a popup could anchor to.
        ui_popup(ID("dock_active_popup"), .anchor = FlPopupAnchor_Point, .point = { 10.0f, 10.0f },
                 .auto_close = false) {
            ui_layout_area(.sizing = { sizing_fixed(40.0f), sizing_fixed(40.0f) }) {}
        }
    }
}

// A headless app and a zeroed DockActiveState named st.
#define DOCK_ACTIVE_APP(app, st)           \
    DOCK_APP(app, "dockactive", 800, 400); \
    DockActiveState st = { 0 }

static void dock_active_settle(FlApp* app, DockActiveState* st) {
    dock_ticks(app, st, dock_active_frame, 3);
}

// Presses the centre of the tile that panel is docked in and holds. False when that tile is not laid out.
static bool dock_press_panel_tile(FlApp* app, DockActiveState* st, FlDockPanelId panel) {
    if (st->fx.ws == nullptr) {
        return false;
    }
    const FlDockNode* leaf = dock_tree_find_leaf_with_panel(st->fx.ws->tree, panel);
    if (leaf == nullptr) {
        return false;
    }
    Clay_ElementData data = dock_element(dock_layout_node_element_id(leaf->id));
    if (!data.found || data.boundingBox.width <= 0.0f) {
        return false;
    }
    dock_press_at(DOCK_RIG(app, st, dock_active_frame), (i32)(data.boundingBox.x + data.boundingBox.width * 0.5f),
                  (i32)(data.boundingBox.y + data.boundingBox.height * 0.5f));
    return true;
}

// Settles the frame, then presses and releases panel's tile. True when the tile was laid out and panel is active.
static bool dock_activate_panel(FlApp* app, DockActiveState* st, FlDockPanelId panel) {
    dock_active_settle(app, st);
    if (!dock_press_panel_tile(app, st, panel)) {
        return false;
    }
    dock_release(DOCK_RIG(app, st, dock_active_frame));
    return st->fx.ws->active_panel == panel;
}

// Ticks once and returns the active panel after it.
static FlDockPanelId dock_active_after_tick(FlApp* app, DockActiveState* st) {
    fl_app_tick(app, st, dock_active_frame);
    return st->fx.ws->active_panel;
}

UTEST(dock_workspace, a_press_activates_a_panel_and_hover_never_does) {
    DOCK_ACTIVE_APP(app, st);
    DOCK_RUN_FRAMES(app, st, st.fx, dock_active_frame, 3);
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 0, "nothing is active before the first press");

    const FlDockNode* right = dock_tree_find_leaf_with_panel(st.fx.ws->tree, 501);
    Clay_ElementData box = dock_element(dock_layout_node_element_id(right->id));
    ASSERT_TRUE_MSG(box.found && box.boundingBox.width > 0.0f, "the right tile was not laid out");
    i32 cx = (i32)(box.boundingBox.x + box.boundingBox.width * 0.5f);
    i32 cy = (i32)(box.boundingBox.y + box.boundingBox.height * 0.5f);

    fl_app_inject_mouse_move(app, cx, cy);
    dock_active_settle(app, &st);
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 0, "hovering a tile must never activate its panel");

    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 501, "a press inside a tile activates its panel");
    dock_release(DOCK_RIG(app, &st, dock_active_frame));

    ASSERT_TRUE(dock_press_panel_tile(app, &st, 500));
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 500, "pressing the other tile moves the active panel");
    dock_release(DOCK_RIG(app, &st, dock_active_frame));
}

UTEST(dock_workspace, changing_the_active_panel_blurs_the_keyboard_ring) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    // Hand the ring to a widget in the other panel. The dock cannot attribute the ring to a panel, which is why
    // the blur below is unconditional.
    const u32 left_button = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("left_button"), .id = 0 });
    st.fx.ctx->focused_id = left_button;
    fl_app_tick(app, &st, dock_active_frame);
    ASSERT_EQ_MSG(fl_focused_id(), left_button, "the button holds the ring while it keeps emitting");

    ASSERT_TRUE(dock_press_panel_tile(app, &st, 500));
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 500, "the press moved the active panel");
    ASSERT_EQ_MSG(fl_focused_id(), 0u, "a change of active panel blurs the keyboard ring");
    dock_release(DOCK_RIG(app, &st, dock_active_frame));

    // The blur survives the frame. Auto-restore repairs a widget that vanished and not a ring nobody claimed.
    fl_app_tick(app, &st, dock_active_frame);
    ASSERT_EQ_MSG(fl_focused_id(), 0u, "nothing re-seeds the ring on the frames after the blur");
}

UTEST(dock_workspace, a_text_input_reclaims_the_ring_the_same_frame) {
    DOCK_ACTIVE_APP(app, st);
    s_dock_active_input = (FlTextInputState) { 0 };
    st.text_input = true;
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 500), "the left panel starts out active");

    const u32 right_input = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("right_input"), .id = 0 });
    ItemState* input = hashmap_get(&st.fx.ctx->shared->item_state, right_input);
    ASSERT_TRUE_MSG(input != nullptr && input->state.size.x > 0.0f, "the right panel's text input was not laid out");
    i32 cx = (i32)(input->state.pos.x + input->state.size.x * 0.5f);
    i32 cy = (i32)(input->state.pos.y + input->state.size.y * 0.5f);

    // A text input claims the ring on the click, so press and release have to land in the same frame for the blur
    // and the claim to collide. The workspace emit runs before the panel bodies, so the claim comes second.
    fl_app_inject_mouse_move(app, cx, cy);
    dock_settle_hover(app, &st, dock_active_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    dock_release(DOCK_RIG(app, &st, dock_active_frame));
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 501, "tapping the input activated its panel");
    ASSERT_EQ_MSG(fl_focused_id(), right_input, "the tapped text input holds the ring the same frame it blurred");
}

UTEST(dock_workspace, hiding_the_active_panel_drops_the_active_id) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    dock_workspace_set_panel_visible(st.fx.ws, 501, false);
    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 0, "a hidden panel cannot stay active");
}

UTEST(dock_workspace, closing_the_active_panel_drops_the_active_id) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    FlDockRequest close = { .kind = FlDockRequest_Close, .panel = 501 };
    ASSERT_TRUE(dock_tree_apply(st.fx.ws->tree, &close));
    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 0, "a closed panel cannot stay active");
}

UTEST(dock_workspace, a_reserved_active_panel_drops_the_active_id) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    st.skip_right = true;
    fl_app_tick(app, &st, dock_active_frame);
    ASSERT_TRUE_MSG(dock_workspace_panel_reserved(st.fx.ws, 501), "the undeclared panel's slot is reserved");
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 0, "a reserved panel cannot stay active");
}

UTEST(dock_workspace, a_dragged_panel_stays_active) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    FlDockTree* tree = st.fx.ws->tree;
    FlDockNodeId right_leaf = dock_tree_find_leaf_with_panel(tree, 501)->id;
    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_active_frame), &st.fx, right_leaf, 0u),
                    "the right leaf's tab was not laid out");
    dock_move_and_tick(DOCK_RIG(app, &st, dock_active_frame), 100, 300,
                       2); // past the arm threshold, over the left half
    ASSERT_EQ_MSG((int)tree->floating_count, 1, "the grabbed panel is in flight");
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 501, "a dragged panel keeps the active id");

    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 501, "and keeps it for as long as the gesture runs");
    dock_release(DOCK_RIG(app, &st, dock_active_frame));
}

UTEST(dock_workspace, showing_a_panel_never_steals_activation) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    dock_workspace_set_panel_visible(st.fx.ws, 500, false);
    fl_app_tick(app, &st, dock_active_frame);
    dock_workspace_set_panel_visible(st.fx.ws, 500, true);
    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 501, "showing another panel does not activate it");
}

UTEST(dock_workspace, reshowing_the_hidden_active_panel_does_not_restore_it) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    dock_workspace_set_panel_visible(st.fx.ws, 501, false);
    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 0, "hiding the active panel drops the id");

    dock_workspace_set_panel_visible(st.fx.ws, 501, true);
    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 0, "showing it again does not resurrect its activation");
}

UTEST(dock_workspace, a_modal_blocks_activation_and_a_popup_does_not) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    st.modal = true;
    dock_active_settle(app, &st);
    ASSERT_TRUE(dock_press_panel_tile(app, &st, 500));
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 501, "a modal blocks a press from moving the active panel");
    dock_release(DOCK_RIG(app, &st, dock_active_frame));

    st.modal = false;
    dock_active_settle(app, &st);
    ASSERT_EQ_MSG((int)st.fx.ctx->modal_scope_id, 0, "the modal closed");

    st.popup = true;
    dock_active_settle(app, &st);
    ASSERT_TRUE_MSG(st.fx.ctx->popup_focus_scope_id != 0, "the popup owns the focus scope");
    ASSERT_TRUE(dock_press_panel_tile(app, &st, 500));
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 500, "a popup does not block panel activation");
    dock_release(DOCK_RIG(app, &st, dock_active_frame));
}
