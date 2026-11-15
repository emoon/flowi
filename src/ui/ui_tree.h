#pragma once

#include <core/types.h>
#include <flowi/ui/tree.h>
#include "ui_selection.h"

typedef struct FlArena FlArena;
typedef struct FlTreeContext FlTreeContext;

FlTreeContext* ui_tree_create(FlArena* arena);

FlListRange ui_tree_begin(FlLayoutId id, const FlTreeRow* rows, u32 count, FlSelectionSet* set, FlTreeConfig config);

FlSignal ui_tree_item_begin(u32 row);

void ui_tree_item_end(void);

FlTreeResult ui_tree_end(void);
