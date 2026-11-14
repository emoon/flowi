#pragma once

// Two pieces:
//   - FlSelectionRequest / FlSelectionRequestKind: the per-frame change unit a List emits.
//   - FlSelectionSet: the caller-owned opaque set of selected stable ids, with an apply that folds a batch of
//     requests in. It is opaque and arena-backed; pair create with destroy.
//
// The List keeps only the gesture anchor across frames; the resolved selection lives only here. The set
// interprets the u64 as pure identity, never as data.

#include <core/types.h>
#include <flowi/ui/selection.h>
#include <flowi/ui/ui_selection.h> // @generated FlSelectionSet (api_gen)
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlArena FlArena;

// FlSelectionRequest / FlSelectionRequestKind are generated into flowi/ui/selection.h, included above.

// Create an empty selection set backed by a dedicated arena. initial_capacity is a hint for the number of ids
// (0 -> built-in default; a floor applies for small values). Pair with destroy.
FlSelectionSet* selection_set_create(u32 initial_capacity);

// Destroy a set from create, freeing its dedicated arena. No-op on nullptr.
void selection_set_destroy(FlSelectionSet* set);

// Membership: the per-frame query a visible row makes to decide its highlight.

// True if id is currently selected.
bool selection_set_contains(const FlSelectionSet* set, u64 id);

// Select (selected = true) or deselect (false) a single id.
void selection_set_set(FlSelectionSet* set, u64 id, bool selected);

// Remove every id (empty the set).
void selection_set_clear_all(FlSelectionSet* set);

// Replace the set's contents with exactly the count ids in ids (clear-then-insert-all). Also the
// resolution of a SetAll(true) request.
void selection_set_set_all(FlSelectionSet* set, const u64* ids, u64 count);

// Folds req_count requests into the set, in array order (sequence is semantics: ClearAll then SetOne(x) nets to
// {x}). ordered_ids (id_count entries) is the app's full ordered model, the enumeration used to resolve a
// span: SetRange flips the inclusive run between its two endpoint ids, and SetAll(true) selects all of them. It
// may be nullptr/0 when no SetRange/SetAll(true) request is present. Endpoints are located by searching this slice
// (order-agnostic); if an endpoint id is absent the range applies to nothing (debug-assert).

void selection_set_apply(FlSelectionSet* set, const FlSelectionRequest* reqs, u64 req_count, const u64* ordered_ids,
                         u64 id_count);

// Number of selected ids.
u64 selection_set_len(const FlSelectionSet* set);

// Snapshot the selected ids into out (capacity cap). Returns the number written (min of len and cap); pass a
// nullptr/0 buffer to size first via len. Order is unspecified (set has no inherent order).
u64 selection_set_copy_ids(const FlSelectionSet* set, u64* out, u64 cap);

#ifdef __cplusplus
}
#endif
