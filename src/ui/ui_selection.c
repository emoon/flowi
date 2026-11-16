// FlSelectionSet implementation (CONTEXT.md Selection Set / Selection Request).
//
// Data structure and protocol only, with no List rendering or gesture code. The set is an opaque handle holding a
// sorted array of disjoint inclusive id ranges, so selecting every row of a huge model is one range.
// apply folds a batch of requests in array order; ranges resolve their endpoints against the
// caller-supplied ordered id list, so off-screen/virtualized rows resolve too.

#include "ui_selection.h"

#include <core/arena.h>
#include <core/heap.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Initial range capacity when the caller passes 0 (a floor also applies for tiny requested values).
#define SELECTION_SET_MIN_CAPACITY 16

typedef struct SelectionRange {
    u64 first;
    u64 last;
} SelectionRange;

struct FlSelectionSet {
    FlArena* arena;         // holds the struct itself; destroy() tears it down wholesale
    SelectionRange* ranges; // heap array, sorted, disjoint and never touching: [first, last] inclusive
    u64 count;
    u64 capacity;
    u64 total; // number of selected ids
};

static u64 range_size(SelectionRange range) {
    return range.last - range.first + 1;
}

static u64 first_ending_at_or_after(const FlSelectionSet* set, u64 id) {
    u64 lo = 0;
    u64 hi = set->count;
    while (lo < hi) {
        u64 mid = lo + (hi - lo) / 2;
        if (set->ranges[mid].last < id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static u64 first_starting_after(const FlSelectionSet* set, u64 id) {
    u64 lo = 0;
    u64 hi = set->count;
    while (lo < hi) {
        u64 mid = lo + (hi - lo) / 2;
        if (set->ranges[mid].first <= id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static void splice(FlSelectionSet* set, u64 at, u64 removed, const SelectionRange* pieces, u64 added) {
    u64 new_count = set->count - removed + added;
    if (new_count > set->capacity) {
        u64 capacity = max(new_count, set->capacity * 2);
        set->ranges = fl_heap_realloc(set->ranges, capacity * sizeof(SelectionRange));
        FL_ASSERT(set->ranges != nullptr);
        set->capacity = capacity;
    }
    memmove(&set->ranges[at + added], &set->ranges[at + removed], (set->count - at - removed) * sizeof(SelectionRange));
    memcpy(&set->ranges[at], pieces, added * sizeof(SelectionRange));
    set->count = new_count;
}

static void remove_span(FlSelectionSet* set, u64 first, u64 last) {
    u64 begin = first_ending_at_or_after(set, first);
    u64 end = first_starting_after(set, last);
    if (begin >= end) {
        return;
    }
    SelectionRange head = set->ranges[begin];
    SelectionRange tail = set->ranges[end - 1];
    for_count(i, end - begin) {
        set->total -= range_size(set->ranges[begin + i]);
    }
    SelectionRange pieces[2];
    u64 kept = 0;
    if (head.first < first) {
        pieces[kept++] = (SelectionRange) { head.first, first - 1 };
    }
    if (tail.last > last) {
        pieces[kept++] = (SelectionRange) { last + 1, tail.last };
    }
    for_count(i, kept) {
        set->total += range_size(pieces[i]);
    }
    splice(set, begin, end - begin, pieces, kept);
}

static bool span_joins_tail(const SelectionRange* tail, u64 first) {
    return first <= tail->last || first - 1 == tail->last;
}

static void append_span(FlSelectionSet* set, u64 first, u64 last) {
    SelectionRange* tail = set->count > 0 ? &set->ranges[set->count - 1] : nullptr;
    if (tail != nullptr && span_joins_tail(tail, first)) {
        if (last > tail->last) {
            set->total += last - tail->last;
            tail->last = last;
        }
        return;
    }
    SelectionRange piece = { first, last };
    set->total += range_size(piece);
    splice(set, set->count, 0, &piece, 1);
}

static void add_span(FlSelectionSet* set, u64 first, u64 last) {
    if (set->count == 0 || set->ranges[set->count - 1].first <= first) {
        append_span(set, first, last);
        return;
    }
    remove_span(set, first, last);
    u64 at = first_starting_after(set, last);
    SelectionRange merged = { first, last };
    u64 replaced = 0;
    if (at > 0 && set->ranges[at - 1].last + 1 == first) {
        merged.first = set->ranges[at - 1].first;
        at--;
        replaced++;
    }
    if (at + replaced < set->count && last + 1 == set->ranges[at + replaced].first) {
        merged.last = set->ranges[at + replaced].last;
        replaced++;
    }
    set->total += last - first + 1;
    splice(set, at, replaced, &merged, 1);
}

static void set_span(FlSelectionSet* set, u64 first, u64 last, bool selected) {
    if (selected) {
        add_span(set, first, last);
    } else {
        remove_span(set, first, last);
    }
}

FlSelectionSet* selection_set_create(u32 initial_capacity) {
    FlArena* arena = arena_new();
    FlSelectionSet* set = arena_alloc_zero(arena, FlSelectionSet);
    set->arena = arena;
    set->capacity = initial_capacity < SELECTION_SET_MIN_CAPACITY ? SELECTION_SET_MIN_CAPACITY : initial_capacity;
    set->ranges = fl_heap_alloc(set->capacity * sizeof(SelectionRange));
    FL_ASSERT(set->ranges != nullptr);
    return set;
}

void selection_set_destroy(FlSelectionSet* set) {
    if (set) {
        fl_heap_free(set->ranges);
        fl_arena_destroy(set->arena);
    }
}

bool selection_set_contains(const FlSelectionSet* set, u64 id) {
    u64 at = first_ending_at_or_after(set, id);
    return at < set->count && set->ranges[at].first <= id;
}

void selection_set_set(FlSelectionSet* set, u64 id, bool selected) {
    if (selection_set_contains(set, id) != selected) {
        set_span(set, id, id, selected);
    }
}

void selection_set_clear_all(FlSelectionSet* set) {
    set->count = 0;
    set->total = 0;
}

static int compare_ids(const void* a, const void* b) {
    u64 left = *(const u64*)a;
    u64 right = *(const u64*)b;
    return (left > right) - (left < right);
}

void selection_set_set_all(FlSelectionSet* set, const u64* ids, u64 count) {
    selection_set_clear_all(set);
    if (count == 0) {
        return;
    }
    u64* sorted = fl_heap_alloc(count * sizeof(u64));
    FL_ASSERT(sorted != nullptr);
    memcpy(sorted, ids, count * sizeof(u64));
    qsort(sorted, count, sizeof(u64), compare_ids);
    for_count(i, count) {
        append_span(set, sorted[i], sorted[i]);
    }
    fl_heap_free(sorted);
}

static void apply_rows_span(FlSelectionSet* set, const ListRows* rows, u32 lo, u32 hi, bool selected) {
    u64 first = rows->id_at(rows, lo);
    u64 last = first;
    for_count(i, hi - lo) {
        u64 id = rows->id_at(rows, lo + 1 + i);
        if (id == last + 1) {
            last = id;
            continue;
        }
        set_span(set, first, last, selected);
        first = id;
        last = id;
    }
    set_span(set, first, last, selected);
}

static void selection_select_all(FlSelectionSet* set, const ListRows* rows) {
    selection_set_clear_all(set);
    if (!list_rows_have_ids(rows)) {
        return;
    }
    apply_rows_span(set, rows, 0, rows->count - 1, true);
}

// Direction-agnostic: flips the inclusive span between the two endpoints.
static void selection_set_range(FlSelectionSet* set, const ListRows* rows, FlSelectionRequest req) {
    i32 from_pos = list_rows_row_of(rows, req.id);
    i32 to_pos = list_rows_row_of(rows, req.to_id);
    bool found = from_pos >= 0 && to_pos >= 0;
    // A missing endpoint signals an app bug (the ordered list is the full model); apply nothing.
    FL_ASSERT_DEBUG(found && "SetRange endpoint id absent from ordered_ids");
    if (!found) {
        return;
    }
    u32 lo = (u32)(from_pos < to_pos ? from_pos : to_pos);
    u32 hi = (u32)(from_pos < to_pos ? to_pos : from_pos);
    apply_rows_span(set, rows, lo, hi, req.selected);
}

static void selection_apply_one(FlSelectionSet* set, const ListRows* rows, FlSelectionRequest req) {
    switch (req.kind) {
        case FlSelectionRequestKind_ClearAll:
            selection_set_clear_all(set);
            break;

        case FlSelectionRequestKind_SetAll:
            if (req.selected) {
                selection_select_all(set, rows);
            } else {
                selection_set_clear_all(set);
            }
            break;

        case FlSelectionRequestKind_SetOne:
            selection_set_set(set, req.id, req.selected);
            break;

        case FlSelectionRequestKind_SetRange:
            selection_set_range(set, rows, req);
            break;
    }
}

void selection_set_apply_rows(FlSelectionSet* set, const FlSelectionRequest* reqs, u64 req_count,
                              const ListRows* rows) {
    for_count(r, req_count) {
        selection_apply_one(set, rows, reqs[r]);
    }
}

void selection_set_apply(FlSelectionSet* set, const FlSelectionRequest* reqs, u64 req_count, const u64* ordered_ids,
                         u64 id_count) {
    ListRows rows = list_rows_from_ids(ordered_ids, nullptr, (u32)id_count);
    selection_set_apply_rows(set, reqs, req_count, &rows);
}

u64 selection_set_len(const FlSelectionSet* set) {
    return set->total;
}

u64 selection_set_copy_ids(const FlSelectionSet* set, u64* out, u64 cap) {
    u64 written = 0;
    if (out == nullptr) {
        return 0;
    }
    for_count(r, set->count) {
        SelectionRange range = set->ranges[r];
        u64 take = min(range_size(range), cap - written);
        for_count(i, take) {
            out[written + i] = range.first + i;
        }
        written += take;
    }
    return written;
}

static u64 shifted_up(u64 id, u64 removed, u64 added) {
    u64 kept = id - removed;
    return kept > UINT64_MAX - added ? UINT64_MAX : kept + added;
}

static void shift_range(FlSelectionSet* set, SelectionRange range, u64 after_id, u64 removed, u64 added) {
    u64 dropped_last = removed > UINT64_MAX - after_id ? UINT64_MAX : after_id + removed;
    if (range.first <= after_id) {
        append_span(set, range.first, min(range.last, after_id));
    }
    if (dropped_last < UINT64_MAX && range.last > dropped_last) {
        u64 first = max(range.first, dropped_last + 1);
        append_span(set, shifted_up(first, removed, added), shifted_up(range.last, removed, added));
    }
}

void selection_set_shift(FlSelectionSet* set, u64 after_id, u64 removed, u64 added) {
    u64 old_count = set->count;
    if (old_count == 0) {
        return;
    }
    SelectionRange* old = fl_heap_alloc(old_count * sizeof(SelectionRange));
    FL_ASSERT(old != nullptr);
    memcpy(old, set->ranges, old_count * sizeof(SelectionRange));
    selection_set_clear_all(set);
    for_count(i, old_count) {
        shift_range(set, old[i], after_id, removed, added);
    }
    fl_heap_free(old);
}
