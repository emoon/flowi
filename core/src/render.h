#pragma once

struct FlGlobalState;

#include "../include/flowi_render.h"

// TODO: Inline?
//u8* Render_create_render_cmd(FlGlobalState* state, FlRenderCommand cmd, int size, int alignment);
//u8* Render_create_render_cmd_mem_1(FlGlobalState* state, FlRenderCommand cmd, u8* data, FlMemoryLifetime lifetime, int size, int alignment);
u8* Render_create_render_cmd_mem_2(struct FlGlobalState* state, FlRenderCommand cmd, void* data0, void* data1, FlMemoryLifetime lifetime, int size, int alignment);

//FcCreateTexture* Render_create_texture_static(FlGlobalState* state, u8* data);

#define Render_create_texture_cmd_static(state, data) \
    Render_create_texture_static(state, FlRc_CreateTexture, data, sizeof(FlRcCreateTexture), 16, FlLifetime_Static)

FlRcCreateTexture* Render_create_texture_static(struct FlGlobalState* state, u8* data);

#define Render_render_flat_triangles_static(state, data0, data1) \
    (FlRcSolidTriangles*)Render_create_render_cmd_mem_2(state, FlRc_RenderTriangles, data0, data1, sizeof(FlRc_RenderTriangles), FlMemoryLifetime_Static, 16)

#define Render_render_texture_triangles_static(state, data0, data1) \
    (FlRcTexturedTriangles*)Render_create_render_cmd_mem_2(state, FlRc_RenderTexturedTriangles, data0, data1, sizeof(FlRc_RenderTexturedTriangles), FlMemoryLifetime_Static, 16)

