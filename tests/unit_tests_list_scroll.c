#include "unit_tests_common.h"

static FlListRange huge_frame(FlUiCtx* ctx, FlLayoutId list_id, u32 total) {
    FlListRange range = { 0 };
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(240) }, .direction = FlLayoutDirection_TopToBottom) {
        range = ui_list_begin(list_id, nullptr, total, nullptr, (FlListConfig) { .row_height = 24.0f, .gap = 2.0f });
        for (u32 i = range.start; i < range.end; i++) {
            ui_list_item_begin((u64)i + 1);
            ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }) {}
            ui_list_item_end();
        }
        ui_list_end();
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return range;
}

static FlElementState huge_row_state(FlLayoutId list_id, u32 row) {
    const FlLayoutId id = { .name = list_id.name, .id = (i32)(row + 1) };
    return ui_get_element_state_by_id(ui_layout_id_to_clay_id(id));
}

static void huge_expect_exact_rows(int* utest_result, FlUiCtx* ctx, FlLayoutId list_id, u32 total) {
    const f64 stride = 26.0;
    huge_frame(ctx, list_id, total);
    huge_frame(ctx, list_id, total);
    list_uniform_nudge_scroll(ctx->list_uniform_ctx, list_id.name, 1.0e12f, (f32)stride, total);
    list_uniform_nudge_scroll(ctx->list_uniform_ctx, list_id.name, -7.0f, (f32)stride, total);
    FlListRange range = huge_frame(ctx, list_id, total);
    range = huge_frame(ctx, list_id, total);

    const f64 offset = list_uniform_scroll_offset(ctx->list_uniform_ctx, list_id.name);
    const FlElementState container
        = ui_get_element_state_by_id(ui_layout_id_to_clay_id((FlLayoutId) { .name = list_id.name, .id = 0 }));
    ASSERT_TRUE_MSG(container.found, "the list container was laid out");
    ASSERT_EQ_MSG(total, range.end, "scrolled to the end, the window reaches the last row");

    u32 checked = 0;
    for (u32 row = range.start; row < range.end; row++) {
        const FlElementState state = huge_row_state(list_id, row);
        if (!state.found || state.size.y <= 0.0f) {
            continue;
        }
        const f64 expected = (f64)container.pos.y + (f64)row * stride - offset;
        ASSERT_TRUE_MSG(fabs((f64)state.pos.y - expected) < 0.01, "row sits on its exact pixel position");
        checked++;
    }
    ASSERT_TRUE_MSG(checked >= 8, "the rows in the viewport were checked");
}

UTEST(list_scroll, rows_stay_exact_in_huge_lists) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ctx->layout_width = 320.0f;
    ctx->layout_height = 256.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 320.0f, .height = 256.0f });

    huge_expect_exact_rows(utest_result, ctx, ID("huge_10k"), 10000);
    huge_expect_exact_rows(utest_result, ctx, ID("huge_1m"), 1000000);
    huge_expect_exact_rows(utest_result, ctx, ID("huge_100m"), 100000000);
    huge_expect_exact_rows(utest_result, ctx, ID("huge_2g"), 2000000000);
}

#define WHEEL_LIST_ROWS 1000

typedef struct WheelListInput {
    f32 mx;
    f32 my;
    f32 wheel_y;
    bool held;
    bool pressed;
    u32 rows;
    FlScrollbarDisplayMode scrollbar_mode;
    FlSelectionSet* set;
} WheelListInput;

static void wheel_list_frame(FlUiCtx* ctx, FlLayoutId list_id, WheelListInput in) {
    static u64 ids[WHEEL_LIST_ROWS];
    for_count(i, WHEEL_LIST_ROWS) {
        ids[i] = (u64)i + 1;
    }

    const u32 rows = in.rows != 0 ? in.rows : WHEEL_LIST_ROWS;

    ui_input_set_mouse_position(ctx, in.mx, in.my);
    ctx->input_state.mouse.buttons[FlMouseButton_Left] = in.held;
    ctx->input_state.mouse.buttons_pressed[FlMouseButton_Left] = in.pressed;
    ui_input_update(ctx, 1.0f / 60.0f);
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = in.wheel_y;

    ui_update_begin(ctx);
    fl_scroll_area_id(FL_ID("wheel_list_area"), .sizing = { sizing_fixed(300), sizing_fixed(200) },
                      .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(60) }) {}
        ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(100) }, .direction = FlLayoutDirection_TopToBottom) {
            FlListRange range = ui_list_begin(
                list_id, ids, rows, in.set,
                (FlListConfig) { .row_height = 24.0f, .gap = 2.0f, .scrollbar_mode = in.scrollbar_mode });
            for (u32 i = range.start; i < range.end; i++) {
                ui_list_item_begin(ids[i]);
                ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }) {}
                ui_list_item_end();
            }
            ui_list_end();
        }
        ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(400) }) {}
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

static FlUiCtx* wheel_list_fixture(void) {
    FlUiCtx* ctx = render_fixture_get();
    if (ctx == nullptr) {
        return nullptr;
    }
    ctx->layout_width = 320.0f;
    ctx->layout_height = 256.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 320.0f, .height = 256.0f });
    return ctx;
}

UTEST(list_scroll, wheel_goes_to_the_list_under_the_cursor) {
    FlUiCtx* ctx = wheel_list_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId list = ID("wheel_list");
    const u32 area_id = ui_layout_id_to_clay_id(FL_ID("wheel_list_area"));

    for_count(i, 3) {
        wheel_list_frame(ctx, list, (WheelListInput) { .mx = 150.0f, .my = 110.0f });
    }
    wheel_list_frame(ctx, list, (WheelListInput) { .mx = 150.0f, .my = 110.0f, .wheel_y = -2.0f });
    for_count(i, 120) {
        wheel_list_frame(ctx, list, (WheelListInput) { .mx = 150.0f, .my = 110.0f });
    }

    const ScrollAreaState* area = hashmap_get(&ctx->shared->scroll_states, area_id);
    ASSERT_TRUE_MSG(area != nullptr, "the enclosing scroll area has state");
    const f64 list_offset = list_uniform_scroll_offset(ctx->list_uniform_ctx, list.name);
    ASSERT_TRUE_MSG(list_offset > 1.0, "the wheel over the list scrolls the list");
    ASSERT_TRUE_MSG(area->scroll_offset.y == 0.0f, "the enclosing scroll area does not also scroll");

    wheel_list_frame(ctx, list, (WheelListInput) { .mx = 150.0f, .my = 30.0f, .wheel_y = -2.0f });
    wheel_list_frame(ctx, list, (WheelListInput) { .mx = 150.0f, .my = 30.0f });
    ASSERT_TRUE_MSG(area->scroll_offset.y > 0.5f, "the wheel outside the list scrolls the enclosing area");
    ASSERT_TRUE_MSG(list_uniform_scroll_offset(ctx->list_uniform_ctx, list.name) == list_offset,
                    "the list stays put when the wheel lands outside it");
}

UTEST(list_scroll, wheel_over_a_list_that_fits_scrolls_the_enclosing_area) {
    FlUiCtx* ctx = wheel_list_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId list = ID("wheel_short_list");
    const u32 area_id = ui_layout_id_to_clay_id(FL_ID("wheel_list_area"));

    for_count(i, 3) {
        wheel_list_frame(ctx, list, (WheelListInput) { .mx = 150.0f, .my = 30.0f, .rows = 2 });
    }
    const ScrollAreaState* area = hashmap_get(&ctx->shared->scroll_states, area_id);
    ASSERT_TRUE_MSG(area != nullptr, "the enclosing scroll area has state");
    const f32 list_y = 60.0f - area->scroll_offset.y + 20.0f;
    const f32 before = area->scroll_target.y;

    wheel_list_frame(ctx, list, (WheelListInput) { .mx = 150.0f, .my = list_y, .wheel_y = 1.0f, .rows = 2 });
    wheel_list_frame(ctx, list, (WheelListInput) { .mx = 150.0f, .my = list_y, .wheel_y = -1.0f, .rows = 2 });
    wheel_list_frame(ctx, list, (WheelListInput) { .mx = 150.0f, .my = list_y, .wheel_y = -1.0f, .rows = 2 });
    ASSERT_TRUE_MSG(area->scroll_target.y != before, "a list with nothing to scroll leaves the wheel to the area");
    ASSERT_TRUE_MSG(list_uniform_scroll_offset(ctx->list_uniform_ctx, list.name) == 0.0, "the list stays put");
}

UTEST(list_scroll, scrollbar_drag_scrolls_without_selecting) {
    FlUiCtx* ctx = wheel_list_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId list = ID("scrollbar_list");
    FlSelectionSet* set = selection_set_create(0);
    const FlScrollbarDisplayMode mode = FlScrollbarDisplayMode_Always;
    const f32 bar_x = 294.0f;

    for_count(i, 4) {
        wheel_list_frame(ctx, list, (WheelListInput) { .mx = bar_x, .my = 120.0f, .scrollbar_mode = mode, .set = set });
    }
    wheel_list_frame(
        ctx, list,
        (WheelListInput) {
            .mx = bar_x, .my = 120.0f, .held = true, .pressed = true, .scrollbar_mode = mode, .set = set });
    for_count(i, 4) {
        wheel_list_frame(
            ctx, list,
            (WheelListInput) { .mx = bar_x, .my = 158.0f, .held = true, .scrollbar_mode = mode, .set = set });
    }

    const f64 max_scroll = (f64)WHEEL_LIST_ROWS * 26.0 - 100.0;
    const f64 offset = list_uniform_scroll_offset(ctx->list_uniform_ctx, list.name);
    ASSERT_TRUE_MSG(offset > max_scroll * 0.9, "dragging the thumb to the bottom scrolls to the end");
    ASSERT_EQ_MSG((u64)0, selection_set_len(set), "a press on the scrollbar does not start a marquee");

    wheel_list_frame(ctx, list, (WheelListInput) { .mx = bar_x, .my = 158.0f, .scrollbar_mode = mode, .set = set });
    selection_set_destroy(set);
}
