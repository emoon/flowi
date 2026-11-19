// declarative ui_list policy tests

#include "list/common.h"

// ui_list(id, items, count, set, opts) takes an array of {id, label, icon} descriptors and the caller-owned set,
// runs the whole interaction internally (virtualization, selection, keyboard nav, marquee, drag-reorder,
// auto-apply) and returns the outcomes on the result. The geometry is the drag case's: row_height 24 + gap 12
// gives a 36px stride in a 120px viewport at the window origin (ui_scale 1, content Y == screen Y at scroll 0).
// Stable ids are (position+1)*10, so a result keyed on position would not match.

#define DECL_ID_AT(p) ((u64)(((p) + 1) * 10))

#define DECL_ROW_X 150.0f  // list column midpoint (the list spans x [0, 300))
#define DECL_ROW0_Y 12.0f  // row 0 midpoint (row 0 spans content [0, 24])
#define DECL_ROW1_Y 48.0f  // row 1 midpoint (row 1 spans content [36, 60])
#define DECL_ROW2_Y 84.0f  // row 2 midpoint (row 2 spans content [72, 96])
#define DECL_ROW2_LO 90.0f // row 2 bottom half ([84, 96]) -> drop After row 2

enum { DECL_ROWS = 10 };

typedef struct DeclCase {
    ListApp app;
    FlListItem items[DECL_ROWS];
    char labels[DECL_ROWS][8];
    const FlListItem* shown_items;
    u32 shown_count;
    FlLayoutId list;
    FlSelectionSet* set;
    FlSelectionSet* shown_set;
} DeclCase;

typedef struct DeclStep {
    ListPointer pointer;
    bool right_held;
    bool enter;
    u32 mods;
    const FlListOpts* opts; // nullptr: multi_select and reorderable both on
} DeclStep;

static void decl_case_init(DeclCase* c) {
    for (u32 i = 0; i < DECL_ROWS; i++) {
        snprintf(c->labels[i], sizeof(c->labels[i]), "row%u", i);
        c->items[i] = (FlListItem) {
            .id = DECL_ID_AT(i),
            .label = string_from_cstr_len(c->labels[i], strlen(c->labels[i])),
            .icon = FL_IMAGE_HANDLE_INVALID, // label-only rows: an invalid handle draws no icon
        };
    }
    c->shown_items = c->items;
    c->shown_count = DECL_ROWS;
    c->list = ID("decl_list");
    c->set = selection_set_create(64);
    c->shown_set = c->set;
}

static FlListResult decl_step_ex(const DeclCase* c, DeclStep step) {
    FlUiCtx* ctx = c->app.ctx;
    FlInputState* in = &ctx->input_state;
    list_pointer_set(ctx, step.pointer);
    in->mouse.buttons[FlMouseButton_Right] = step.right_held;
    // right_clicked fires once on the press edge, so the press edge is set too. The cases hold right for a single
    // frame, so right_held serves as the edge.
    in->mouse.buttons_pressed[FlMouseButton_Right] = step.right_held;
    in->keyboard.keys_pressed[FlKey_Enter] = step.enter; // one-frame edge that ui_input_update clears afterward
    in->keyboard.modifiers = step.mods;                   // held modifiers, read by the click gesture code
    ui_input_update(ctx, 1.0f / 60.0f);

    const FlListOpts both_on = { .row_height = 24.0f, .gap = 12.0f, .multi_select = true, .reorderable = true };
    const FlListOpts opts = step.opts != nullptr ? *step.opts : both_on;
    FlListResult lr = { 0 };
    ui_update_begin(ctx);
    ui_layout_area_id(ID("decl_root"), .sizing = { sizing_fixed(300), sizing_fixed(120) },
                      .direction = LayoutDirection_TopToBottom) {
        lr = ui_list(c->list, c->shown_items, c->shown_count, c->shown_set, opts);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return lr;
}

static FlListResult decl_step(const DeclCase* c, ListPointer pointer) {
    return decl_step_ex(c, (DeclStep) { .pointer = pointer });
}

// Runs the same step several times.
static void decl_repeat(const DeclCase* c, DeclStep step, int frames) {
    for (int f = 0; f < frames; f++) {
        decl_step_ex(c, step);
    }
}

// Lets hover settle on the row under the cursor. Hover takes two frames to propagate.
static void decl_dwell(const DeclCase* c, float y) {
    decl_repeat(c, (DeclStep) { .pointer = list_ptr_hover(DECL_ROW_X, y) }, 4);
}

// (1) Activation. Dwell so row 1 is hot, click it to focus the list and put the keyboard cursor on row 1, then
// press Enter on the next frame to activate the cursor row.
static void decl_phase_activation(int* utest_result, DeclCase* c) {
    decl_dwell(c, DECL_ROW1_Y);
    decl_step(c, list_ptr_press(DECL_ROW_X, DECL_ROW1_Y));
    decl_step(c, list_ptr_release(DECL_ROW_X, DECL_ROW1_Y)); // release -> click
    ASSERT_TRUE_MSG(selection_set_contains(c->set, DECL_ID_AT(1)), "a plain click auto-applies into the caller's set");
    ASSERT_EQ_MSG((u64)1, selection_set_len(c->set), "plain click selects exactly the clicked row");

    FlListResult act = decl_step_ex(c, (DeclStep) { .pointer = list_ptr_hover(DECL_ROW_X, DECL_ROW1_Y), .enter = true });
    ASSERT_EQ_MSG(DECL_ID_AT(1), act.activated_id, "Enter activates the focused cursor row (the clicked row)");
}

// (2) Context-menu id. The List reports the row and opens no menu itself. Move the cursor onto row 2, dwell for
// the hover lag, then hold the right button.
static void decl_phase_context_menu(int* utest_result, DeclCase* c) {
    decl_dwell(c, DECL_ROW2_Y);
    FlListResult menu = decl_step_ex(c, (DeclStep) { .pointer = list_ptr_hover(DECL_ROW_X, DECL_ROW2_Y),
                                                     .right_held = true });
    ASSERT_EQ_MSG(DECL_ID_AT(2), menu.context_menu_id, "right-click reports the row's stable id as the menu target");
    ASSERT_EQ_MSG((u64)0, menu.activated_id, "a right-click is not an activation");
}

// (3) Reorder outcome. Press row 0, drag past the threshold into row 2's bottom half, release.
static void decl_phase_reorder(int* utest_result, DeclCase* c) {
    decl_dwell(c, DECL_ROW0_Y);
    decl_step(c, list_ptr_press(DECL_ROW_X, DECL_ROW0_Y)); // arms
    FlListResult mid = decl_step(c, list_ptr_drag(DECL_ROW_X, DECL_ROW2_LO));
    ASSERT_TRUE_MSG(mid.drag_active, "crossing the threshold begins the reorder drag");
    FlListResult drop = decl_step(c, list_ptr_release(DECL_ROW_X, DECL_ROW2_LO));
    ASSERT_TRUE_MSG(drop.reorder_emitted, "releasing an active drag emits a reorder request");
    ASSERT_EQ_MSG((u32)1, drop.drag_from_count, "an unselected pressed row drags just itself");
    const u64 dragged_id = drop.drag_from_ids[0];
    ASSERT_EQ_MSG(DECL_ID_AT(0), dragged_id, "the dragged id is the pressed row's stable id");
    ASSERT_EQ_MSG(DECL_ID_AT(2), drop.drag_target_id, "dropped in row 2's bottom half -> target is row 2");
    ASSERT_EQ_MSG((int)FlDropRelationship_After, (int)drop.drag_rel, "bottom half of a row -> drop After it");
}

// The remaining phases run on the same fl_app, because a second fl_app_create would mount the VFS twice. Each phase
// uses a new list id so no gesture anchor or focus carries over.

// (4) multi_select = false: the extend and toggle modifiers are ignored, so a ctrl-click replaces the selection and
// it never grows past one row. Click row 0, then ctrl-click row 2.
static void decl_phase_single_select(int* utest_result, DeclCase* c) {
    const FlListOpts single = { .row_height = 24.0f, .gap = 12.0f, .multi_select = false, .reorderable = true };
    selection_set_clear_all(c->set);
    c->list = ID("decl_single");
    decl_repeat(c, (DeclStep) { .pointer = list_ptr_hover(DECL_ROW_X, DECL_ROW0_Y), .opts = &single }, 4);
    decl_step_ex(c, (DeclStep) { .pointer = list_ptr_press(DECL_ROW_X, DECL_ROW0_Y), .opts = &single });
    decl_step_ex(c, (DeclStep) { .pointer = list_ptr_release(DECL_ROW_X, DECL_ROW0_Y), .opts = &single }); // click row 0
    ASSERT_TRUE_MSG(selection_set_contains(c->set, DECL_ID_AT(0)), "single-select: plain click selects row 0");

    const DeclStep ctrl = { .mods = FlKeyModifier_Control, .opts = &single };
    DeclStep step = ctrl;
    step.pointer = list_ptr_hover(DECL_ROW_X, DECL_ROW2_Y);
    decl_repeat(c, step, 4);
    step.pointer = list_ptr_press(DECL_ROW_X, DECL_ROW2_Y);
    decl_step_ex(c, step); // ctrl-press row 2
    step.pointer = list_ptr_release(DECL_ROW_X, DECL_ROW2_Y);
    decl_step_ex(c, step); // ctrl-release -> ctrl-click row 2
    ASSERT_EQ_MSG((u64)1, selection_set_len(c->set), "single-select ignores ctrl: the selection never grows past one");
    ASSERT_TRUE_MSG(selection_set_contains(c->set, DECL_ID_AT(2)), "single-select ctrl-click replaces with row 2");
    ASSERT_FALSE_MSG(selection_set_contains(c->set, DECL_ID_AT(0)), "single-select drops the previously selected row");
}

// (5) reorderable = false: a press and drag past the threshold does not begin a reorder.
static void decl_phase_no_reorder(int* utest_result, DeclCase* c) {
    const FlListOpts no_reorder = { .row_height = 24.0f, .gap = 12.0f, .multi_select = true, .reorderable = false };
    c->list = ID("decl_noreorder");
    selection_set_clear_all(c->set);
    decl_repeat(c, (DeclStep) { .pointer = list_ptr_hover(DECL_ROW_X, DECL_ROW0_Y), .opts = &no_reorder }, 2);
    decl_step_ex(c, (DeclStep) { .pointer = list_ptr_press(DECL_ROW_X, DECL_ROW0_Y), .opts = &no_reorder });
    FlListResult mid = decl_step_ex(c, (DeclStep) { .pointer = list_ptr_drag(DECL_ROW_X, DECL_ROW2_LO), // past threshold
                                                    .opts = &no_reorder });
    ASSERT_FALSE_MSG(mid.drag_active, "reorderable=false: crossing the threshold does not begin a drag");
    FlListResult drop = decl_step_ex(c, (DeclStep) { .pointer = list_ptr_release(DECL_ROW_X, DECL_ROW2_LO),
                                                     .opts = &no_reorder });
    ASSERT_FALSE_MSG(drop.reorder_emitted, "reorderable=false: releasing emits no reorder request");
}

// (6) count == 0 and items == nullptr both render an empty list with a zero result. The null-items case passes a
// non-zero count to show that ui_list ignores the count when there is no array.
static void decl_phase_empty(int* utest_result, DeclCase* c) {
    const FlListOpts no_reorder = { .row_height = 24.0f, .gap = 12.0f, .multi_select = true, .reorderable = false };
    const DeclStep offscreen = { .pointer = list_ptr_hover(-100.0f, -100.0f), .opts = &no_reorder };
    c->shown_set = nullptr;
    c->list = ID("decl_empty");
    c->shown_count = 0;
    FlListResult empty = decl_step_ex(c, offscreen);
    ASSERT_EQ_MSG((u64)0, empty.activated_id, "empty list: nothing activated");
    ASSERT_EQ_MSG((u64)0, empty.context_menu_id, "empty list: no context menu");
    ASSERT_FALSE_MSG(empty.reorder_emitted, "empty list: no reorder");

    c->list = ID("decl_null");
    c->shown_items = nullptr;
    c->shown_count = DECL_ROWS;
    FlListResult null_items = decl_step_ex(c, offscreen);
    ASSERT_EQ_MSG((u64)0, null_items.activated_id, "nullptr items with a non-zero count is treated as empty (no crash)");
}

UTEST(list_declarative, policy_outcomes) {
    DeclCase c;
    ASSERT_TRUE_MSG(list_app_open(&c.app, FL_S("list_declarative")), "fl_app_create(headless) failed");
    decl_case_init(&c);

    LIST_RUN_PHASE(decl_phase_activation(utest_result, &c));
    LIST_RUN_PHASE(decl_phase_context_menu(utest_result, &c));
    LIST_RUN_PHASE(decl_phase_reorder(utest_result, &c));
    LIST_RUN_PHASE(decl_phase_single_select(utest_result, &c));
    LIST_RUN_PHASE(decl_phase_no_reorder(utest_result, &c));
    LIST_RUN_PHASE(decl_phase_empty(utest_result, &c));

    selection_set_destroy(c.set);
    list_app_close(&c.app);
}
