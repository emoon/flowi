///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Selection - FlSelectionSet implementation (CONTEXT.md Selection Set / Selection Request).
//
// Pure data structure + protocol: no List rendering or gesture code. The set is an arena-backed opaque handle
// wrapping base's u64-keyed hashmap used as a set (the value slot is an unused dummy byte - identity only).
// apply folds a batch of requests in array order; ranges resolve their endpoints against the
// caller-supplied ordered id list, so off-screen/virtualized rows resolve too.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lifecycle

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Membership

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Apply

// Linear search for id in the ordered model; ranges are rare (one per shift-select), so no auxiliary index.
static bool selection_find_pos(const u64* ordered_ids, u64 id_count, u64 id, u64* out_pos) {
    for (u64 i = 0; i < id_count; i++) {
        if (ordered_ids[i] == id) {
            *out_pos = i;
            return true;
        }
    }
    return false;
}

void selection_set_apply(FlSelectionSet* set, const FlSelectionRequest* reqs, u64 req_count, const u64* ordered_ids,
                         u64 id_count) {
    for (u64 r = 0; r < req_count; r++) {
        FlSelectionRequest req = reqs[r];
        switch (req.kind) {
            case FlSelectionRequestKind_ClearAll:
                selection_set_clear_all(set);
                break;

            case FlSelectionRequestKind_SetAll:
                if (req.selected) {
                    selection_set_set_all(set, ordered_ids, id_count);
                } else {
                    selection_set_clear_all(set);
                }
                break;

            case FlSelectionRequestKind_SetOne:
                selection_set_set(set, req.id, req.selected);
                break;

            case FlSelectionRequestKind_SetRange: {
                u64 from_pos, to_pos;
                bool found = selection_find_pos(ordered_ids, id_count, req.id, &from_pos)
                             && selection_find_pos(ordered_ids, id_count, req.to_id, &to_pos);
                if (found) {
                    // Direction-agnostic: flip the inclusive span between the two endpoints.
                    u64 lo = from_pos < to_pos ? from_pos : to_pos;
                    u64 hi = from_pos < to_pos ? to_pos : from_pos;
                    for (u64 i = lo; i <= hi; i++) {
                        selection_set_set(set, ordered_ids[i], req.selected);
                    }
                } else {
                    // A missing endpoint signals an app bug (the ordered list is the full model); apply nothing.
                    FL_ASSERT_DEBUG(found && "SetRange endpoint id absent from ordered_ids");
                }
                break;
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read-out

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
