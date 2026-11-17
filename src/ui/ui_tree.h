#pragma once

#include <core/types.h>
#include <flowi/ui/tree.h>

typedef struct FlArena FlArena;
typedef struct FlListContext FlListContext;
typedef struct FlTreeContext FlTreeContext;

// Layout id of a row's expand-arrow slot. Counts up from INT32_MIN, clear of the list's header ids that count down from
// -1.
#define TREE_ARROW_ID(row) ((i32)(INT32_MIN + (i32)(row)))

FlTreeContext* ui_tree_create(FlArena* arena, FlListContext* list);
