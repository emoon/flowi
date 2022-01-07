#pragma once

#include "../include/flowi_render.h"
#include "command_buffer.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define Render_create_texture_cmd(state) \
    (FlRcCreateTexture*)CommandBuffer_alloc_cmd(&state->render_commands, FlRc_CreateTexture, sizeof(FlRcCreateTexture))

#define Render_render_flat_triangles_cmd(state) \
    (FlRcSolidTriangles*)CommandBuffer_alloc_cmd(&state->render_commands, FlRc_RenderTriangles, sizeof(FlRcSolidTriangles))

#define Render_render_texture_triangles_cmd(state) \
    (FlRcTexturedTriangles*)CommandBuffer_alloc_cmd(&state->render_commands, FlRc_RenderTexturedTriangles, sizeof(FlRcTexturedTriangles))

#define Render_update_texture_cmd(state) \
    (FlRcUpdateTexture*)CommandBuffer_alloc_cmd(&state->render_commands, FlRc_UpdateTexture, sizeof(FlRcUpdateTexture))

