#pragma once

#include "types.h"

#include <flowi/heap/heap.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Malloc-family heap allocator, backed by mimalloc (or the system allocator when sanitizers are on or
// on Windows).
//
// This is not an arena: allocations are individually owned and must be released with fl_heap_free (or
// fl_heap_free_aligned for fl_heap_alloc_aligned). Use <core/arena.h> for bulk allocations freed together.
