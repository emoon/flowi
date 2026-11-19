#pragma once

// Internal header for the List widget (CONTEXT.md `## Lists`). The scoped face (fl_list_begin /
// fl_list_item_begin / fl_list_item_end / fl_list_end) is declared in <flowi/ui/ui_list.h>. The List reuses
// list_uniform's virtualization core.

#include <core/types.h>
#include <flowi/ui/ui_list.h>
#include <ui/list/list_api.h>
#include "../ui_selection.h"

typedef struct FlArena FlArena;

FlListContext* ui_list_create(FlArena* arena);

// Declarative face (CONTEXT.md `## Lists`), the one-call API for the common case. Hand it an array of simple
// FlListItem descriptors ({ id, label, icon }, count entries) and the caller-owned set. It runs the whole list
// internally (virtualization, the selection gesture machine, keyboard nav, marquee, drag-reorder, the
// highlight/ghost/insertion-line drawing, and auto-applying selection into set) and renders each row's label and
// icon. The caller reads only policy outcomes off the result (activated_id, context_menu_id, the reorder
// request) and needs no per-item loop, selection_set_apply or Signal handling. opts carries the layout knobs
// plus the multi_select / reorderable policy toggles. Use the scoped face instead for rows with custom
// content.
FlListResult ui_list(FlLayoutId id, const FlListItem* items, u32 count, FlSelectionSet* set, FlListOpts opts);
