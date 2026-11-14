#include <flowi/core/assert.h>
#include <flowi/heap/heap.h>

#define KB_TEXT_SHAPE_NO_CRT
#define KBTS_MALLOC(Data, Size) fl_heap_alloc((Size))
#define KBTS_FREE(Data, Pointer) fl_heap_free((Pointer))
#define KBTS_ASSERT(Cond) FL_ASSERT_DEBUG(Cond)

#define KB_TEXT_SHAPE_IMPLEMENTATION
#include "kb_text_shape.h"
