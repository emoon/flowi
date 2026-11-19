#pragma once

// Shared fixtures for the unit_tests_dock_*.c translation units: tree builders, the headless app rig, pointer
// helpers, and readers for the frame's render commands.

#include "unit_tests_common.h"

// Tree helpers.

// The first panel of a leaf node, or 0 if it is empty.
FlDockPanelId dock_leaf_panel0(const FlDockNode* leaf);

// Two single-panel leaves, a and b, in an even X split.
FlDockTree* dock_pair_tree(FlDockPanelId a, FlDockPanelId b);

// Docks panel as a tab into target_node. True when the request applied and the tree still passes its invariants.
bool dock_tab_into(FlDockTree* tree, FlDockNodeId target_node, FlDockPanelId panel);

// Docks each of panels as a tab into target_node, in order. True when every request applied and the tree still passes
// its invariants.
bool dock_tab_panels(FlDockTree* tree, FlDockNodeId target_node, const FlDockPanelId* panels, u32 count);

#define DOCK_TAB_PANELS(tree, node, ...)                                     \
    dock_tab_panels((tree), (node), (const FlDockPanelId[]) { __VA_ARGS__ }, \
                    sizeof_array(((const FlDockPanelId[]) { __VA_ARGS__ })))

// Deep structural equality of two trees.
bool dock_logical_equal(const FlDockNode* a, const FlDockNode* b);

// Returns from the enclosing case once an assertion in a helper it called has failed.
#define DOCK_STOP_ON_FAILURE()                    \
    do {                                          \
        if (*utest_result != UTEST_TEST_PASSED) { \
            return;                               \
        }                                         \
    } while (0)

// The expected contents of a leaf: its tab count, the selected index and the panels in tab order.
enum { DOCK_WANT_PANELS_MAX = 8 };

typedef struct DockLeafWant {
    u32 count;
    u32 selected;
    FlDockPanelId panels[DOCK_WANT_PANELS_MAX];
} DockLeafWant;

// Asserts leaf holds the want->count panels of want, in tab order. The selected index is not compared.
void dock_expect_tab_order(int* utest_result, const FlDockNode* leaf, const DockLeafWant* want);

// Asserts node is a leaf holding want. The selected index is compared only for a non-empty leaf.
void dock_expect_leaf(int* utest_result, const FlDockNode* node, const DockLeafWant* want);

// Asserts tree's panel universe, docked plus floating, holds count panels.
void dock_expect_universe(int* utest_result, FlDockTree* tree, u32 count);

// What a workspace has to report about one panel. A field left at DockTri_Any is not checked.
typedef enum DockTri { DockTri_Any = 0, DockTri_Yes, DockTri_No } DockTri;

typedef struct PanelWant {
    FlDockPanelId panel;
    DockTri emit;
    DockTri declared;
    DockTri reserved;
    DockTri visible;
} PanelWant;

void dock_expect_panel_states(int* utest_result, FlDockWorkspace* ws, const PanelWant* rows, u32 count);

// The leaf node a workspace has to resolve a panel to. Node 0 stands for no leaf.
typedef struct PanelLeaf {
    FlDockPanelId panel;
    FlDockNodeId node;
} PanelLeaf;

void dock_expect_panel_leaves(int* utest_result, FlDockWorkspace* ws, const PanelLeaf* rows, u32 count);

// Asserts box sits on target: the same origin and the same size, within a pixel.
void dock_expect_box_on(int* utest_result, Clay_BoundingBox box, Clay_BoundingBox target);

// Asserts the title the workspace retains for panel reads back as want.
void dock_expect_title(int* utest_result, FlDockWorkspace* ws, FlDockPanelId panel, FlString want);

// Frame fixture for the live cases, which all run the headless fl_app loop. Every frame captures the frame's
// context and workspace, installs a default tree on the first frame a workspace exists, then opens the workspace
// and emits its tiles.

typedef struct DockFixture {
    FlUiCtx* ctx;
    FlDockWorkspace* ws;
    bool installed;
} DockFixture;

// A headless app that lives for one case. dock_app_close runs on every exit from the case, including an ASSERT
// return, so the app never outlives it and the pointer is never left pressed.
typedef struct DockApp {
    FlApp* app;
} DockApp;

DockApp dock_app_open(FlString title, i32 width, i32 height);
void dock_app_close(DockApp* scope);

// Declares `FlApp* app`: a headless window of the given size with a fixed 1/60 s step per tick, destroyed when the
// case returns.
#define DOCK_APP(app, title, width, height)                                                                       \
    __attribute__((cleanup(dock_app_close))) DockApp app##_scope = dock_app_open(FL_S(title), (width), (height)); \
    ASSERT_TRUE_MSG(app##_scope.app != nullptr, "render fixture or headless fl_app_create failed");               \
    FlApp* app = app##_scope.app

// The app, the frame state and the frame callback that drive one case.
typedef struct DockRig {
    FlApp* app;
    void* st;
    void (*frame)(void*);
} DockRig;

#define DOCK_RIG(app, st, frame) (&(const DockRig) { (app), (st), (frame) })

// Captures ctx and ws into st, installs spec as the tree the first time a workspace exists, then begins the
// workspace and emits its tiles into a grow/grow area. Returns the workspace, or nullptr before one exists. The
// caller declares any panel bodies and then calls dock_workspace_end on the returned workspace.
FlDockWorkspace* dock_fixture_emit(DockFixture* st, const FlDockSpec* spec);

// Ticks the app n times with frame.
void dock_ticks(FlApp* app, void* st, void (*frame)(void*), int n);

// Asserts a two-panel gesture ended with nothing in the holding pen and both panels still in the tree.
#define DOCK_ASSERT_GESTURE_ENDED(tree)                                                              \
    do {                                                                                             \
        ASSERT_EQ_MSG((int)(tree)->floating_count, 0, "nothing is left floating after the gesture"); \
        ASSERT_EQ_MSG((int)dock_tree_collect_panels((tree), nullptr, 0), 2,                          \
                      "both panels are conserved across the gesture");                               \
    } while (0)

// Ticks n frames, then asserts the fixture saw a context and a workspace in them.
#define DOCK_RUN_FRAMES(app, state, fx, frame, n)                                                       \
    do {                                                                                                \
        dock_ticks((app), &(state), (frame), (n));                                                      \
        ASSERT_TRUE_MSG((fx).ctx != nullptr && (fx).ws != nullptr, "frame did not run / no workspace"); \
    } while (0)

// Ticks until an injected pointer move has settled. The move is pumped at a tick boundary and the behaviour pass
// reads the previous frame's hit test, so hover arrives two frames late. Four ticks leave margin.
void dock_settle_hover(FlApp* app, void* st, void (*frame)(void*));

// Hovers (cx, cy) until it goes hot, then presses without releasing, which arms the drag latch on the hot element.
void dock_press_at(const DockRig* rig, i32 cx, i32 cy);

// Moves the pointer to (x, y) and ticks n frames.
void dock_move_and_tick(const DockRig* rig, i32 x, i32 y, int n);

// Releases the left button and ticks once.
void dock_release(const DockRig* rig);

// The element data Clay laid out for a Clay id, or for a layout id.
Clay_ElementData dock_element(u32 clay_id);
Clay_ElementData dock_layout_element(FlLayoutId id);

// The Clay element id of the index-th tab of leaf leaf_id.
u32 dock_tab_clay_id(FlDockNodeId leaf_id, u32 index);

// A pointer position in physical px.
typedef struct DockPoint {
    i32 x;
    i32 y;
} DockPoint;

// The centre of a laid-out item, or of a Clay box.
DockPoint dock_item_centre(const ItemState* item);
DockPoint dock_box_centre(Clay_BoundingBox box);

// The centre in px of the leaf's index-th dock tab. False when the tab was not laid out.
bool dock_tab_centre(const DockFixture* fx, FlDockNodeId leaf_id, u32 index, DockPoint* out);

// Presses and holds the centre of the leaf's index-th dock tab. False when the tab was not laid out.
bool dock_press_tab(const DockRig* rig, const DockFixture* fx, FlDockNodeId leaf_id, u32 index);

// A single root leaf holding two panels {100, 101}, with panel 100 selected at first.
void dock_tab_header_frame(void* user_data);

// A panel body that is one marker element filling its tile, keyed by panel id so each rect can be found.
void dock_nest_panel(FlDockPanelId id);

// True if a reported size is the {0,0} answer.
bool dock_size_is_zero(FlVec2 size);

// The laid-out width of node's tile, or -1 when it was not laid out.
f32 dock_node_width(FlDockNodeId node);

// Whether this frame drew a text command equal to want, optionally restricted to one z band. The drag ghost carries
// the held panel's title, so only the band tells a tab label from the ghost.
#define DOCK_ANY_Z (-1)
bool frame_has_text(FlString want);
bool frame_has_text_at_z(FlString want, int z);

// Whether id drew this frame. Clay's element data answers for the last frame that filled it, so it cannot show
// that something stopped rendering.
bool frame_has_rect(FlLayoutId id);

// The default workspace's tree, replaced for the case and put back when the scope ends.
typedef struct DockDefaultTree {
    FlDockWorkspace* ws;
    FlDockTree* saved;
} DockDefaultTree;

DockDefaultTree dock_default_tree_swap(FlUiCtx* fixture, const FlDockSpec* spec);
void dock_default_tree_restore(DockDefaultTree* scope);

#define DOCK_DEFAULT_TREE(var, fixture, spec) \
    __attribute__((cleanup(dock_default_tree_restore))) DockDefaultTree var = dock_default_tree_swap((fixture), (spec))
