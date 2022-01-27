
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <flowi_core/render_commands.h>
#include "command_buffer.h"

#define Render_textured_triangles_cmd(state) \
    (FlTexturedTriangles*)CommandBuffer_alloc_cmd(&state->render_commands, FlRenderCommand_TexturedTriangles, sizeof(FlTexturedTriangles))

#define Render_solid_triangles_cmd(state) \
    (FlSolidTriangles*)CommandBuffer_alloc_cmd(&state->render_commands, FlRenderCommand_SolidTriangles, sizeof(FlSolidTriangles))

#define Render_create_texture_cmd(state) \
    (FlCreateTexture*)CommandBuffer_alloc_cmd(&state->render_commands, FlRenderCommand_CreateTexture, sizeof(FlCreateTexture))

#define Render_update_texture_cmd(state) \
    (FlUpdateTexture*)CommandBuffer_alloc_cmd(&state->render_commands, FlRenderCommand_UpdateTexture, sizeof(FlUpdateTexture))

#define Render_scissor_rect_cmd(state) \
    (FlScissorRect*)CommandBuffer_alloc_cmd(&state->render_commands, FlRenderCommand_ScissorRect, sizeof(FlScissorRect))

