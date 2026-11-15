#pragma once

#include <core/types.h>
#include <flowi/ui/tree.h>

typedef struct FlArena FlArena;
typedef struct FlListContext FlListContext;
typedef struct FlTreeContext FlTreeContext;

FlTreeContext* ui_tree_create(FlArena* arena, FlListContext* list);
