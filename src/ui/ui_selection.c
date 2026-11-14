// FlSelectionSet implementation (CONTEXT.md Selection Set / Selection Request).
//
// Data structure and protocol only, with no List rendering or gesture code. The set is an arena-backed opaque
// handle wrapping the core u64-keyed hashmap used as a set (the value slot is an unused dummy byte).
// apply folds a batch of requests in array order; ranges resolve their endpoints against the
// caller-supplied ordered id list, so off-screen/virtualized rows resolve too.

#include "ui_selection.h"

#include <core/arena.h>
#include <core/hashmap.h>

#include <assert.h>

// Initial bucket count when the caller passes 0 (a floor also applies for tiny requested values).
#define SELECTION_SET_MIN_CAPACITY 16

struct FlSelectionSet {
    FlArena* arena;       // dedicated arena; destroy() tears it down wholesale
    hashmap(u64, u8) map; // u64 stable id -> dummy byte; membership is "key present"
};

FlSelectionSet* selection_set_create(u32 initial_capacity) {
    FlArena* arena = arena_new();
    FlSelectionSet* set = arena_alloc_zero(arena, FlSelectionSet);
    set->arena = arena;
    u64 capacity = initial_capacity < SELECTION_SET_MIN_CAPACITY ? SELECTION_SET_MIN_CAPACITY : initial_capacity;
    hashmap_new(&set->map, arena, capacity);
    return set;
}

void selection_set_destroy(FlSelectionSet* set) {
    if (set) {
        fl_arena_destroy(set->arena);
    }
}

bool selection_set_contains(const FlSelectionSet* set, u64 id) {
    return hashmap_contains(&set->map, id);
}

void selection_set_set(FlSelectionSet* set, u64 id, bool selected) {
    if (selected) {
        hashmap_insert(&set->map, id, (u8)1);
    } else {
        hashmap_remove(&set->map, id);
    }
}

void selection_set_clear_all(FlSelectionSet* set) {
    hashmap_clear(&set->map);
}

void selection_set_set_all(FlSelectionSet* set, const u64* ids, u64 count) {
    hashmap_clear(&set->map);
    for (u64 i = 0; i < count; i++) {
        hashmap_insert(&set->map, ids[i], (u8)1);
    }
}

static void selection_select_all(FlSelectionSet* set, const ListRows* rows) {
    selection_set_clear_all(set);
    if (!list_rows_have_ids(rows)) {
        return;
    }
    for_count(i, rows->count) {
        hashmap_insert(&set->map, rows->id_at(rows, i), (u8)1);
    }
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
    for (u32 i = lo; i <= hi; i++) {
        selection_set_set(set, rows->id_at(rows, i), req.selected);
    }
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
    return hashmap_count(&set->map);
}

u64 selection_set_copy_ids(const FlSelectionSet* set, u64* out, u64 cap) {
    u64 written = 0;
    for_each_hashmap(&set->map, key, value) {
        (void)value;
        if (out && written < cap) {
            out[written] = *key;
            written++;
        }
    }
    return written;
}
