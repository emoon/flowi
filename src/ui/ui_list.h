#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// List API - Internal Implementation Header
//
// Internal header for the List widget (CONTEXT.md `## Lists`). The scoped face (ui_list_begin /
// ui_list_item_begin / ui_list_item_end / ui_list_end) is declared below. The List reuses list_uniform's
// virtualization core.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <core/types.h>
#include <ui/list_api.h>
#include "ui_selection.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlArena FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlListContext* ui_list_create(FlArena* arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scoped face - operate on the global UI context. These are the symbols the api_gen seam (ui_list.def) binds
// to Rust; keep their names in lockstep with the seam.

// Begin a virtualized list over the app's full ordered id slice (ordered_ids, count entries). count is
// both the virtualization total and the model length forwarded to selection_set_apply. set is the
// caller-owned selection storage the List auto-applies emitted requests into and resolves the highlight from;
// pass NULL to opt out of auto-apply/highlight (the app then owns them via result.requests). ordered_ids is
// borrowed for the whole begin->end scope and is required when set is non-NULL (may be NULL when set is
// NULL). Returns the visible index range to emit.
FlListRange ui_list_begin(FlLayoutId id, const u64* ordered_ids, u32 count, FlSelectionSet* set, FlListConfig config);

// Begin one row carrying the opaque stable_id; returns its interaction signal (computed from the previous
// frame's rect, with selected resolved from the set's id membership). The List auto-tracks the row's
// position by emission order and never interprets the id.
FlSignal ui_list_item_begin(u64 stable_id);

void ui_list_item_end(void);

// End the list; the per-frame result (activated_id / focused_id) is keyed on stable ids.
FlListResult ui_list_end(void);

// Request keyboard focus for the list id on a popup's open edge. Engages the list's own keyboard
// handling (has_focus) AND claims the global focus ring (ui_ctx->focused_id) together, so the focus sticks past
// the frame the trigger widget blurs. Seeds the focus cursor to cursor_id (a stable Item Id; 0 = first row),
// validated against the ordered slice in begin with fallback to the first row. Opt-in and re-arm guarded: the
// cursor is seeded only when the list did not already hold focus, so a stray every-frame call is redundant.
void ui_list_request_focus(FlLayoutId id, u64 cursor_id);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Declarative face (CONTEXT.md `## Lists`). The common-case one-call API: hand it an array of simple
// FlListItem descriptors ({ id, label, icon }, count entries) and the caller-owned set, and it runs the
// whole list internally - virtualization, the selection gesture machine, keyboard nav, marquee, drag-reorder,
// the highlight/ghost/insertion-line drawing, and auto-applying selection into set - while rendering each
// row's label + icon. The caller reads only policy outcomes off the result (activated_id, context_menu_id,
// the reorder request); no per-item loop, no selection_set_apply, no Signal handling. opts carries the
// layout knobs plus the multi_select / reorderable policy toggles. Use the scoped face above instead for
// rows with custom content.
FlListResult ui_list(FlLayoutId id, const FlListItem* items, u32 count, FlSelectionSet* set, FlListOpts opts);
