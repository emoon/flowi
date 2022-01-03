#pragma once

#include "types.h"

struct Atlas;
struct FlAllocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Atlas* Atlas_create(int w, int h, int pre_alloc, struct FlAllocator* allocator);
void Atlas_destroy(struct Atlas* atlas);

bool Atlas_add_rect(struct Atlas* atlas, int rw, int rh, int* rx, int* ry);

