#include "internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Used for casting the start of a struct that holds pointers so we don't need to care about the concrete type

typedef struct Pointers {
    void* p0;
    void* p1;
} Pointers;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u8* alloc_internal(FlGlobalState* state, FlRenderCommand cmd, int size, u64 alignment) {
    FL_UNLIKELY(alignment);

    BuildRenderState* render_data = &state->render_data;
    // TODO: Check if we actually need to align the data

    // Align the pointer up to correct alignment
    u8* start = render_data->render_data;
    u8* end_data = start + size;
    render_data->render_data = end_data;

#if FL_VALIDATE_RANGES
    if (FL_UNLIKELY(end_data > render_data->end_render_data ||
                    render_data->render_commands > render_data->render_commands_end)) {
        ERROR_ADD(FlError_Memory, "Out of memory in RenderCommand Allocator", filename);
        return NULL;
    }
#endif

    *render_data->render_commands++ = (u8)cmd;

    return start;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u8* Render_alloc_command_internal(FlGlobalState* state, FlRenderCommand cmd, int size, int alignment) {
    return alloc_internal(state, cmd, size, alignment);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u8* Render_create_render_cmd_mem_2(FlGlobalState* state, FlRenderCommand cmd, void* d0, void* d1, int size, int align) {
    Pointers* cmd_write = (Pointers*)alloc_internal(state, cmd, size, align);
#if FL_VALIDATE_RANGES
    if (FL_UNLIKELY(!cmd)) {
        return NULL;
    }
#endif

    cmd_write->p0 = d0;
    cmd_write->p1 = d1;
    return (u8*)cmd_write;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlRcCreateTexture* Render_create_texture_static(FlGlobalState* state, u8* data) {
    FlRcCreateTexture* texture = (FlRcCreateTexture*)alloc_internal(
        state, FlRc_CreateTexture, sizeof(FlRcCreateTexture), FL_ALIGNOF(FlRcCreateTexture));

    // TODO: Error return macro
#if FL_VALIDATE_RANGES
    if (!texture) {
        return NULL;
    }
#endif

    // As the data is static we just assign it directly and grab a texture id
    texture->data = data;
    texture->id = state->texture_ids++;

    return texture;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process all the rendering commands and output vertex/index buffers

void Render_process_commands(FlGlobalState* state) {
	FL_UNUSED(state);
}
