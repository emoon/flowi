#include "primitives.h"
#include "internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u8* alloc_internal(FlGlobalState* state, Primitive cmd, int size) {
    BuildPrimitives* prim_data = &state->primitives_data;

    u8* start = prim_data->data;
    u8* end_data = start + size;
    prim_data->data = end_data;

#if FL_VALIDATE_RANGES
    if (FL_UNLIKELY(end_data > prim_data->end_data)) {
        ERROR_ADD(FlError_Memory, "Out of memory in PrimitiveCommand Allocator");
        return NULL;
    }
#endif

    *start++ = (u8)cmd;
    start[size - 1] = 0;	// TODO: write end marker of data. This isn't great cache wise, maybe do somethin better here

    return start;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u8* Primitive_alloc_cmd(FlGlobalState* state, Primitive cmd, int size) {
    return alloc_internal(state, cmd, size);
}



