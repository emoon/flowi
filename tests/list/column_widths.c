// list column width resolution tests

#include "list/common.h"
#include "ui/list/list_columns.h"

// Column width resolution (list_resolve_column_widths): a pure function from ColumnDefs and the available width
// to one width per column. The cases use whole numbers, and the asserts allow a small tolerance anyway.

#define COL_EPS 0.001

static void expect_widths(int* utest_result, const f32* got, const f32* want, u32 count) {
    for (u32 i = 0; i < count; i++) {
        ASSERT_NEAR((double)want[i], (double)got[i], COL_EPS);
    }
}

UTEST(list_columns, fixed_columns_keep_their_width) {
    const FlColumnDef cols[] = { { .id = 1, .width = 100 }, { .id = 2, .width = 80 }, { .id = 3, .width = 60 } };
    f32 out[3] = { 0 };
    list_resolve_column_widths(cols, 3, 500.0f, out);
    expect_widths(utest_result, out, (const f32[]) { 100.0f, 80.0f, 60.0f }, 3);
}

UTEST(list_columns, a_fixed_width_below_the_minimum_is_raised) {
    const FlColumnDef cols[] = { { .id = 1, .width = 20, .min_width = 50 } };
    f32 out[1] = { 0 };
    list_resolve_column_widths(cols, 1, 500.0f, out);
    expect_widths(utest_result, out, (const f32[]) { 50.0f }, 1);
}

UTEST(list_columns, flexible_columns_split_the_leftover_by_weight) {
    const FlColumnDef cols[] = {
        { .id = 1, .width = 100 }, { .id = 2, .flex = 1 }, { .id = 3, .flex = 3 }, { .id = 4 },
    };
    f32 out[4] = { 0 };
    list_resolve_column_widths(cols, 4, 500.0f, out);
    // 400 left: weights 1, 3 and 1 (a zero flex counts as 1).
    expect_widths(utest_result, out, (const f32[]) { 100.0f, 80.0f, 240.0f, 80.0f }, 4);
}

UTEST(list_columns, a_share_below_the_minimum_pins_the_column_and_the_others_split_the_rest) {
    const FlColumnDef cols[] = {
        { .id = 1, .flex = 1, .min_width = 150 }, { .id = 2, .flex = 1 }, { .id = 3, .flex = 1 },
    };
    f32 out[3] = { 0 };
    list_resolve_column_widths(cols, 3, 300.0f, out);
    // An even split is 100 each, so column 1 pins at 150 and the other two share the remaining 150.
    expect_widths(utest_result, out, (const f32[]) { 150.0f, 75.0f, 75.0f }, 3);
}

UTEST(list_columns, minimums_wider_than_the_panel_leave_every_column_at_its_minimum) {
    const FlColumnDef cols[] = {
        { .id = 1, .width = 200 }, { .id = 2, .flex = 1, .min_width = 90 }, { .id = 3, .flex = 2, .min_width = 70 },
    };
    f32 out[3] = { 0 };
    list_resolve_column_widths(cols, 3, 250.0f, out);
    expect_widths(utest_result, out, (const f32[]) { 200.0f, 90.0f, 70.0f }, 3);
}

UTEST(list_columns, fixed_columns_wider_than_the_panel_still_get_their_width) {
    const FlColumnDef cols[] = { { .id = 1, .width = 300 }, { .id = 2, .flex = 1 } };
    f32 out[2] = { 0 };
    list_resolve_column_widths(cols, 2, 200.0f, out);
    expect_widths(utest_result, out, (const f32[]) { 300.0f, 0.0f }, 2);
}

UTEST(list_columns, zero_columns_write_nothing) {
    f32 out[1] = { 7.0f };
    list_resolve_column_widths(nullptr, 0, 500.0f, out);
    expect_widths(utest_result, out, (const f32[]) { 7.0f }, 1);
}
