#pragma once

// Shared fixtures for the unit_tests_list*.c translation units: a list model, the frame emitters over it, the
// selection-gesture frames and the headless-app fixture of the marquee, drag and declarative cases.

#include "unit_tests_common.h"

#define LIST_REQUIRE_FIXTURE(ctx)                                                       \
    FlUiCtx* ctx = render_fixture_get();                                                \
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed")

// Runs a phase helper that asserts through utest_result, and returns from the case when the phase failed.
#define LIST_RUN_PHASE(call) \
    do {                     \
        call;                \
        if (*utest_result != 0) { \
            return;          \
        }                    \
    } while (0)

enum { LIST_MODEL_MAX_ROWS = 100 };

// The rows a test list emits. Stable ids are (position + 1) until list_model_scale_ids changes them.
typedef struct ListModel {
    FlLayoutId list;
    FlSelectionSet* set;
    FlListConfig config;
    u64 ids[LIST_MODEL_MAX_ROWS];
    u32 count;
    f32 viewport_h;
} ListModel;

// Rows are 24 high with a gap of 4, and the model has no selection set.
void list_model_init(ListModel* model, FlLayoutId list, u32 count, f32 viewport_h);

void list_model_scale_ids(ListModel* model, u64 factor);

typedef struct ListFrameOpts {
    FlListRange* out_range;   // the range fl_list_begin returned
    FlSignal* out_signals;    // each visible row's Signal at its absolute index
    FlNavFrame* out_nav;      // the nav_frame after fl_list_end
    u32* out_global_focus;    // the global focus ring id after fl_list_end
    bool request_focus;       // calls fl_list_request_focus just before the list begins
    u64 focus_seed;
} ListFrameOpts;

// Emits the model as one frame, in a 300 wide area of the model's viewport height.
FlListResult list_emit_frame(FlUiCtx* ctx, const ListModel* model, const ListFrameOpts* opts);

void list_prepare_surface(FlUiCtx* ctx);

void list_park_cursor(FlUiCtx* ctx);

void list_release_mouse(FlUiCtx* ctx);

typedef struct ListPointer {
    float x;
    float y;
    bool held;
    bool pressed;
    bool released;
} ListPointer;

static inline ListPointer list_ptr_hover(float x, float y) {
    return (ListPointer) { .x = x, .y = y };
}

static inline ListPointer list_ptr_press(float x, float y) {
    return (ListPointer) { .x = x, .y = y, .held = true, .pressed = true };
}

static inline ListPointer list_ptr_drag(float x, float y) {
    return (ListPointer) { .x = x, .y = y, .held = true };
}

static inline ListPointer list_ptr_release(float x, float y) {
    return (ListPointer) { .x = x, .y = y, .released = true };
}

// Writes the pointer into the input state without running ui_input_update.
void list_pointer_set(FlUiCtx* ctx, ListPointer pointer);

// A headless app with its global UI context at scale 1, so the list is at the window origin and content Y equals
// screen Y.
typedef struct ListApp {
    FlApp* app;
    FlUiCtx* ctx;
} ListApp;

bool list_app_open(ListApp* app, FlString title);

void list_app_close(ListApp* app);

typedef struct ListBoxedStep {
    FlLayoutId root;
    ListPointer pointer;
    u32 modifiers;
    bool nav_down;
    FlListResult* out_result;
} ListBoxedStep;

// One frame of the model in a 300 by 120 area with the given root id, driven by the real input layer. Modifiers and
// nav_down are written after ui_input_update so the input layer cannot clear them.
FlListRange list_boxed_step(FlUiCtx* ctx, const ListModel* model, const ListBoxedStep* step);

typedef struct SelInput {
    u32 modifiers;
    bool key_a;
    bool click;
    FlSignal* out_signals;
} SelInput;

// Stamps one visible row hot and clears hot on every other row. hot_pos < 0 clears all.
void list_sel_only_hot(FlUiCtx* ctx, const ListModel* model, int hot_pos);

// One frame of the selection test list with the cursor parked off the list.
FlListResult list_sel_frame(FlUiCtx* ctx, const ListModel* model, SelInput input);

// Sizes the surface, runs two idle frames and returns the visible range from a third.
FlListRange list_sel_setup(FlUiCtx* ctx, const ListModel* model);

// Stamps the row hot and clicks it with the given modifiers.
FlListResult list_sel_click_row(FlUiCtx* ctx, const ListModel* model, int pos, u32 modifiers);
