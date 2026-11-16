// FlSelectionSet range storage tests

#include "unit_tests_common.h"

static bool selection_holds(const FlSelectionSet* set, const u64* expected, u64 count) {
    u64 ids[64];
    u64 written = selection_set_copy_ids(set, ids, 64);
    if (selection_set_len(set) != count || written != count) {
        return false;
    }
    for_count(i, count) {
        if (ids[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

static FlSelectionSet* selection_with_run(u64 first, u64 last) {
    FlSelectionSet* set = selection_set_create(0);
    for (u64 id = first; id <= last; id++) {
        selection_set_set(set, id, true);
    }
    return set;
}

UTEST(selection_ranges, adjacent_ids_join_into_one_run) {
    const u64 picked[] = { 1, 2, 4, 3 };
    FlSelectionSet* set = selection_set_create(0);
    for_count(i, 4) {
        selection_set_set(set, picked[i], true);
    }
    EXPECT_EQ(selection_set_len(set), 4u);
    EXPECT_TRUE(selection_set_contains(set, 3));
    selection_set_destroy(set);
}

UTEST(selection_ranges, deselecting_the_middle_splits_a_run) {
    FlSelectionSet* set = selection_with_run(1, 5);
    selection_set_set(set, 3, false);
    const u64 expected[] = { 1, 2, 4, 5 };
    EXPECT_TRUE(selection_holds(set, expected, 4));
    EXPECT_FALSE(selection_set_contains(set, 3));
    EXPECT_TRUE(selection_set_contains(set, 4));
    selection_set_destroy(set);
}

UTEST(selection_ranges, deselecting_an_unselected_id_changes_nothing) {
    FlSelectionSet* set = selection_with_run(5, 6);
    selection_set_set(set, 9, false);
    selection_set_set(set, 5, true);
    const u64 expected[] = { 5, 6 };
    EXPECT_TRUE(selection_holds(set, expected, 2));
    selection_set_destroy(set);
}

UTEST(selection_ranges, a_range_request_absorbs_the_runs_it_crosses) {
    FlSelectionSet* set = selection_set_create(0);
    selection_set_set(set, 1, true);
    selection_set_set(set, 4, true);
    selection_set_set(set, 9, true);
    const u64 ordered[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    FlSelectionRequest request = { .kind = FlSelectionRequestKind_SetRange, .id = 3, .to_id = 8, .selected = true };
    selection_set_apply(set, &request, 1, ordered, 10);
    const u64 expected[] = { 1, 3, 4, 5, 6, 7, 8, 9 };
    EXPECT_TRUE(selection_holds(set, expected, 8));
    selection_set_destroy(set);
}

UTEST(selection_ranges, select_all_over_ids_that_are_not_consecutive_keeps_each_run) {
    FlSelectionSet* set = selection_set_create(0);
    const u64 ordered[] = { 10, 11, 20, 21, 22, 40 };
    FlSelectionRequest request = { .kind = FlSelectionRequestKind_SetAll, .selected = true };
    selection_set_apply(set, &request, 1, ordered, 6);
    const u64 expected[] = { 10, 11, 20, 21, 22, 40 };
    EXPECT_TRUE(selection_holds(set, expected, 6));
    selection_set_destroy(set);
}

UTEST(selection_ranges, set_all_replaces_the_set_sorted_without_duplicates) {
    FlSelectionSet* set = selection_with_run(100, 120);
    const u64 ids[] = { 9, 3, 5, 4, 3 };
    selection_set_set_all(set, ids, 5);
    const u64 expected[] = { 3, 4, 5, 9 };
    EXPECT_TRUE(selection_holds(set, expected, 4));
    EXPECT_FALSE(selection_set_contains(set, 100));
    selection_set_destroy(set);
}

UTEST(selection_ranges, the_largest_ids_do_not_overflow) {
    FlSelectionSet* set = selection_set_create(0);
    selection_set_set(set, UINT64_MAX, true);
    selection_set_set(set, UINT64_MAX - 1, true);
    selection_set_set(set, 0, true);
    EXPECT_EQ(selection_set_len(set), 3u);
    selection_set_set(set, UINT64_MAX, false);
    const u64 expected[] = { 0, UINT64_MAX - 1 };
    EXPECT_TRUE(selection_holds(set, expected, 2));
    selection_set_destroy(set);
}

UTEST(selection_ranges, select_all_of_a_million_rows_is_cheap_and_exact) {
    FlSelectionSet* set = selection_set_create(0);
    ListRows rows = list_rows_from_count(1000000);
    FlSelectionRequest all = { .kind = FlSelectionRequestKind_SetAll, .selected = true };
    selection_set_apply_rows(set, &all, 1, &rows);
    EXPECT_EQ(selection_set_len(set), 1000000u);
    EXPECT_TRUE(selection_set_contains(set, 1));
    EXPECT_TRUE(selection_set_contains(set, 1000000));
    EXPECT_FALSE(selection_set_contains(set, 1000001));
    selection_set_set(set, 500000, false);
    EXPECT_EQ(selection_set_len(set), 999999u);
    EXPECT_FALSE(selection_set_contains(set, 500000));
    selection_set_destroy(set);
}

typedef struct ShiftCase {
    u64 after_id;
    u64 removed;
    u64 added;
    u64 expected[16];
    u64 expected_count;
} ShiftCase;

UTEST(selection_ranges, shifting_rows_moves_drops_and_rejoins_ids) {
    const ShiftCase cases[] = {
        { 3, 2, 0, { 1, 2, 3, 4, 5, 6, 7, 8 }, 8 },
        { 3, 0, 2, { 1, 2, 3, 6, 7, 8, 9, 10, 11, 12 }, 10 },
        { 3, 4, 0, { 1, 2, 3, 4, 5, 6 }, 6 },
        { 0, UINT64_MAX, 0, { 0 }, 0 },
        { 5, 0, 0, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }, 10 },
    };
    for_count(c, 5) {
        FlSelectionSet* set = selection_with_run(1, 10);
        selection_set_shift(set, cases[c].after_id, cases[c].removed, cases[c].added);
        EXPECT_TRUE(selection_holds(set, cases[c].expected, cases[c].expected_count));
        selection_set_destroy(set);
    }
}
