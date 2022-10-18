// clang-format off
#include <assert.h>
#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include <flowi/application.h>
#include <flowi_core/style.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../shaders/generated/color_fill.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
// clang-format off

// TODO: Should be in public core api
//#include "../../../core/c/src/area.h"
#include "../../../core/c/src/flowi.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const bgfx::EmbeddedShader s_shaders[] = {BGFX_EMBEDDED_SHADER(color_fill_vs),
                                                 BGFX_EMBEDDED_SHADER(color_fill_fs), 
                                                 BGFX_EMBEDDED_SHADER(vs_texture),
                                                 BGFX_EMBEDDED_SHADER(fs_texture), 
                                                 BGFX_EMBEDDED_SHADER(vs_texture_r),
                                                 BGFX_EMBEDDED_SHADER(fs_texture_r), 
                                                 BGFX_EMBEDDED_SHADER_END()};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Texture {
    bgfx::TextureHandle handle;
    bgfx::TextureFormat::Enum format;
    int size;
    int width;
    int height;
    float inv_x;
    float inv_y;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define MAX_TEXTURE_COUNT 128
#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct ApplicationState {
    struct FlGlobalState* flowi_state;
    struct FlContext* ctx;
    int window_width;
    int window_height;
    int counter;

    // TODO: Don't hardcode
    Texture textures[MAX_TEXTURE_COUNT];
    // layout and shader for rendering non-textured triangles
    bgfx::VertexLayout flat_layout;
    bgfx::ProgramHandle flat_shader;
    // layout and shader for rendering textured triangles
    bgfx::VertexLayout texture_layout;
    bgfx::ProgramHandle texture_shader;
    bgfx::ProgramHandle texture_r_shader;

    bgfx::UniformHandle tex_handle;
    bgfx::UniformHandle u_inv_res_tex;

    GLFWwindow* default_window;

    FlMainLoopCallback main_callback;
    void* user_data;
};

static ApplicationState s_state;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* native_window_handle(GLFWwindow* window) {
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
    return (void*)(uintptr_t)glfwGetX11Window(window);
#elif BX_PLATFORM_OSX
    return glfwGetCocoaWindow(window);
#elif BX_PLATFORM_WINDOWS
    return glfwGetWin32Window(window);
#endif  // BX_PLATFORM_
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)action;
    (void)mods;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void error_callback(int error, const char* description) {
    fprintf(stderr, "Error: %d:%s\n", error, description);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" struct FlContext* fl_application_create_impl(FlString application_name, FlString developer) {
    (void)application_name;
    (void)developer;

    ApplicationState* state = &s_state;

    // TODO: Error, we only support one application so make sure we only run this once.
    if (state->ctx != NULL) {
        printf("Application already created\n");
        return NULL;
    }

    if (state->window_width == 0) {
        state->window_width = WINDOW_WIDTH;
    }

    if (state->window_height == 0) {
        state->window_height = WINDOW_HEIGHT;
    }

    // This to be called before using any other functions
    // TODO: Proper error
    if (!(state->flowi_state = fl_create(NULL))) {
        printf("Unable to create flowi state\n");
        return 0;
    }

    state->ctx = fl_context_create(state->flowi_state);

    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        // TODO: Proper error
        printf("failed to init glfw\n");
        return NULL;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_FLOATING, GL_FALSE);

    state->default_window = glfwCreateWindow(state->window_width, state->window_height, "Fix me title", NULL, NULL);
    if (!state->default_window) {
        printf("failed to open window\n");
        glfwTerminate();
        return NULL;
    }

    bgfx::PlatformData pd;
#if defined(GLFW_EXPOSE_NATIVE_X11)
    pd.ndt = glfwGetX11Display();
#endif
    pd.nwh = native_window_handle(state->default_window);
    pd.context = NULL;
    pd.backBuffer = NULL;
    pd.backBufferDS = NULL;

    glfwSetKeyCallback(state->default_window, key_callback);

    bgfx::setPlatformData(pd);

    int reset_flags = BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X8;

    bgfx::Init bgfxInit;
    bgfxInit.type = bgfx::RendererType::OpenGL;
    bgfxInit.resolution.width = state->window_width;
    bgfxInit.resolution.height = state->window_height;
    bgfxInit.resolution.reset = reset_flags;
    bgfxInit.platformData = pd;

    if (!bgfx::init(bgfxInit)) {
        printf("failed to init bgfx\n");
        glfwDestroyWindow(state->default_window);
        glfwTerminate();
        return NULL;
    }

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x2f2f2fff, 1.0f, 0);

    state->flat_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    state->texture_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Int16, false, true)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    bgfx::RendererType::Enum type = bgfx::getRendererType();

    state->flat_shader = bgfx::createProgram(bgfx::createEmbeddedShader(s_shaders, type, "color_fill_vs"),
                                             bgfx::createEmbeddedShader(s_shaders, type, "color_fill_fs"));

    state->texture_shader = bgfx::createProgram(bgfx::createEmbeddedShader(s_shaders, type, "vs_texture"),
                                                bgfx::createEmbeddedShader(s_shaders, type, "fs_texture"));

    state->texture_r_shader = bgfx::createProgram(bgfx::createEmbeddedShader(s_shaders, type, "vs_texture_r"),
                                                  bgfx::createEmbeddedShader(s_shaders, type, "fs_texture_r"));

    if (!bgfx::isValid(state->flat_shader)) {
        printf("failed to init flat_shader shaders\n");
        return NULL;
    }

    if (!bgfx::isValid(state->texture_shader)) {
        printf("failed to init texture_shader shaders\n");
        return NULL;
    }

    if (!bgfx::isValid(state->texture_r_shader)) {
        printf("failed to init texture_r_shader shaders\n");
        return NULL;
    }

    state->u_inv_res_tex = bgfx::createUniform("u_inv_res_tex", bgfx::UniformType::Vec4);

    return state->ctx;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render triangles without texture

static void render_textured_triangles(ApplicationState& ctx, const u8* render_data, bgfx::Encoder* encoder/*, const FlStyle& style*/) {
    FlTexturedTriangles* draw_cmd = (FlTexturedTriangles*)render_data;

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;

    const int vertex_count = draw_cmd->vertex_buffer_size;
    const int index_count = draw_cmd->index_buffer_size;
    const u32 texture_id = draw_cmd->texture_id;

    const Texture& texture = ctx.textures[texture_id];

    // TODO: We can remove all of these copies as the vertexbuffers are double buffered and can be passed as ref
    bgfx::allocTransientVertexBuffer(&tvb, vertex_count, ctx.texture_layout);
    bgfx::allocTransientIndexBuffer(&tib, index_count, sizeof(FlIdxSize) == 4);

    void* verts = (void*)tvb.data;
    memcpy(verts, draw_cmd->vertex_buffer, vertex_count * sizeof(FlVertPosUvColor));

    u16* indices = (u16*)tib.data;
    memcpy(indices, draw_cmd->index_buffer, index_count * sizeof(FlIdxSize));

    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;
    state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

    // Set 1/texture size for shader
    float data[4] = {texture.inv_x, texture.inv_y, 0.0f, 0.0f};
    encoder->setUniform(ctx.u_inv_res_tex, data, UINT16_MAX);

    encoder->setState(state);
    encoder->setTexture(0, ctx.tex_handle, texture.handle);
    encoder->setVertexBuffer(0, &tvb, 0, vertex_count);
    encoder->setIndexBuffer(&tib, 0, index_count);

    if (texture.format == bgfx::TextureFormat::R8) {
        encoder->submit(255, ctx.texture_r_shader);
    } else {
        encoder->submit(255, ctx.texture_shader);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render triangles without texture

static void render_flat_triangles(ApplicationState& ctx, const u8* render_data, bgfx::Encoder* encoder/*, const FlStyle& style*/) {
    FlSolidTriangles* draw_cmd = (FlSolidTriangles*)render_data;

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;

    const int vertex_count = draw_cmd->vertex_buffer_size;
    const int index_count = draw_cmd->index_buffer_size;

    bgfx::allocTransientVertexBuffer(&tvb, vertex_count, ctx.flat_layout);
    bgfx::allocTransientIndexBuffer(&tib, index_count, sizeof(FlIdxSize) == 4);

    void* verts = (void*)tvb.data;
    memcpy(verts, draw_cmd->vertex_buffer, vertex_count * sizeof(FlVertPosColor));

    u16* indices = (u16*)tib.data;
    memcpy(indices, draw_cmd->index_buffer, index_count * sizeof(FlIdxSize));

    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;
    state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

    encoder->setState(state);
    encoder->setVertexBuffer(0, &tvb, 0, vertex_count);
    encoder->setIndexBuffer(&tib, 0, index_count);
    encoder->submit(255, ctx.flat_shader);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void create_texture(ApplicationState& ctx, const u8* render_data) {
    const FlCreateTexture* cmd = (FlCreateTexture*)render_data;
    const u8* data = cmd->data;
    const u32 id = cmd->id;
    const u16 width = cmd->width;
    const u16 height = cmd->height;

    assert(id < MAX_TEXTURE_COUNT);

    switch (cmd->format) {
        case FlTextureFormat_R8Linear: {
            const bgfx::Memory* mem = nullptr;

            if (data) {
                mem = bgfx::makeRef(data, width * height);
            }

            bgfx::TextureFormat::Enum format = bgfx::TextureFormat::R8;

            Texture* texture = &ctx.textures[id];
            texture->handle = bgfx::createTexture2D(width, height, false, 1, format, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, mem);
            texture->inv_x = 1.0f / width;
            texture->inv_y = 1.0f / height;
            texture->size = width * height;
            texture->height = height;
            texture->width = width;
            texture->format = format;
            break;
        }

        case FlTextureFormat_Rgba8Srgb: {
            const bgfx::Memory* mem = nullptr;

            if (data) {
                mem = bgfx::makeRef(data, width * height * 4);
            }

            bgfx::TextureFormat::Enum format = bgfx::TextureFormat::RGBA8;

            Texture* texture = &ctx.textures[id];
            texture->handle = bgfx::createTexture2D(width, height, false, 1, format, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, mem);
            texture->inv_x = 1.0f / width;
            texture->inv_y = 1.0f / height;
            texture->size = width * height * 4;
            texture->height = height;
            texture->width = width;
            texture->format = format;
            break;
        }

        default: {
            // TODO: Implement support
            printf("unsupported texture format %d\n", cmd->format);
            exit(0);
        }
    }
}

#if 0

/*
 *
 * Copyright 2014-2015 Daniel Collin. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bx/allocator.h>
#include <bx/math.h>
#include <bx/timer.h>
#include <dear-imgui/imgui.h>
#include <dear-imgui/imgui_internal.h>

#include "imgui.h"
#include "../bgfx_utils.h"

//#define USE_ENTRY 1

#ifndef USE_ENTRY
#	define USE_ENTRY 0
#endif // USE_ENTRY

#if USE_ENTRY
#	include "../entry/entry.h"
#	include "../entry/input.h"
#endif // USE_ENTRY

#include "vs_ocornut_imgui.bin.h"
#include "fs_ocornut_imgui.bin.h"
#include "vs_imgui_image.bin.h"
#include "fs_imgui_image.bin.h"

#include "roboto_regular.ttf.h"
#include "robotomono_regular.ttf.h"
#include "icons_kenney.ttf.h"
#include "icons_font_awesome.ttf.h"

static const bgfx::EmbeddedShader s_embeddedShaders[] =
{
	BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
	BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
	BGFX_EMBEDDED_SHADER(vs_imgui_image),
	BGFX_EMBEDDED_SHADER(fs_imgui_image),

	BGFX_EMBEDDED_SHADER_END()
};

struct FontRangeMerge
{
	const void* data;
	size_t      size;
	ImWchar     ranges[3];
};

static FontRangeMerge s_fontRangeMerge[] =
{
	{ s_iconsKenneyTtf,      sizeof(s_iconsKenneyTtf),      { ICON_MIN_KI, ICON_MAX_KI, 0 } },
	{ s_iconsFontAwesomeTtf, sizeof(s_iconsFontAwesomeTtf), { ICON_MIN_FA, ICON_MAX_FA, 0 } },
};

static void* memAlloc(size_t _size, void* _userData);
static void memFree(void* _ptr, void* _userData);

struct OcornutImguiContext
{
	void render(ImDrawData* _drawData)
	{
		// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
		int fb_width = (int)(_drawData->DisplaySize.x * _drawData->FramebufferScale.x);
		int fb_height = (int)(_drawData->DisplaySize.y * _drawData->FramebufferScale.y);
		if (fb_width <= 0 || fb_height <= 0)
			return;

		bgfx::setViewName(m_viewId, "ImGui");
		bgfx::setViewMode(m_viewId, bgfx::ViewMode::Sequential);

		const bgfx::Caps* caps = bgfx::getCaps();
		{
			float ortho[16];
			float x = _drawData->DisplayPos.x;
			float y = _drawData->DisplayPos.y;
			float width = _drawData->DisplaySize.x;
			float height = _drawData->DisplaySize.y;

			bx::mtxOrtho(ortho, x, x + width, y + height, y, 0.0f, 1000.0f, 0.0f, caps->homogeneousDepth);
			bgfx::setViewTransform(m_viewId, NULL, ortho);
			bgfx::setViewRect(m_viewId, 0, 0, uint16_t(width), uint16_t(height) );
		}

		const ImVec2 clipPos   = _drawData->DisplayPos;       // (0,0) unless using multi-viewports
		const ImVec2 clipScale = _drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

		// Render command lists
		for (int32_t ii = 0, num = _drawData->CmdListsCount; ii < num; ++ii)
		{
			bgfx::TransientVertexBuffer tvb;
			bgfx::TransientIndexBuffer tib;

			const ImDrawList* drawList = _drawData->CmdLists[ii];
			uint32_t numVertices = (uint32_t)drawList->VtxBuffer.size();
			uint32_t numIndices  = (uint32_t)drawList->IdxBuffer.size();

			if (!checkAvailTransientBuffers(numVertices, m_layout, numIndices) )
			{
				// not enough space in transient buffer just quit drawing the rest...
				break;
			}

			bgfx::allocTransientVertexBuffer(&tvb, numVertices, m_layout);
			bgfx::allocTransientIndexBuffer(&tib, numIndices, sizeof(ImDrawIdx) == 4);

			ImDrawVert* verts = (ImDrawVert*)tvb.data;
			bx::memCopy(verts, drawList->VtxBuffer.begin(), numVertices * sizeof(ImDrawVert) );

			ImDrawIdx* indices = (ImDrawIdx*)tib.data;
			bx::memCopy(indices, drawList->IdxBuffer.begin(), numIndices * sizeof(ImDrawIdx) );

			bgfx::Encoder* encoder = bgfx::begin();

			for (const ImDrawCmd* cmd = drawList->CmdBuffer.begin(), *cmdEnd = drawList->CmdBuffer.end(); cmd != cmdEnd; ++cmd)
			{
				if (cmd->UserCallback)
				{
					cmd->UserCallback(drawList, cmd);
				}
				else if (0 != cmd->ElemCount)
				{
					uint64_t state = 0
						| BGFX_STATE_WRITE_RGB
						| BGFX_STATE_WRITE_A
						| BGFX_STATE_MSAA
						;

					bgfx::TextureHandle th = m_texture;
					bgfx::ProgramHandle program = m_program;

					if (NULL != cmd->TextureId)
					{
						union { ImTextureID ptr; struct { bgfx::TextureHandle handle; uint8_t flags; uint8_t mip; } s; } texture = { cmd->TextureId };
						state |= 0 != (IMGUI_FLAGS_ALPHA_BLEND & texture.s.flags)
							? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA)
							: BGFX_STATE_NONE
							;
						th = texture.s.handle;
						if (0 != texture.s.mip)
						{
							const float lodEnabled[4] = { float(texture.s.mip), 1.0f, 0.0f, 0.0f };
							bgfx::setUniform(u_imageLodEnabled, lodEnabled);
							program = m_imageProgram;
						}
					}
					else
					{
						state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
					}

					// Project scissor/clipping rectangles into framebuffer space
					ImVec4 clipRect;
					clipRect.x = (cmd->ClipRect.x - clipPos.x) * clipScale.x;
					clipRect.y = (cmd->ClipRect.y - clipPos.y) * clipScale.y;
					clipRect.z = (cmd->ClipRect.z - clipPos.x) * clipScale.x;
					clipRect.w = (cmd->ClipRect.w - clipPos.y) * clipScale.y;

					if (clipRect.x <  fb_width
					&&  clipRect.y <  fb_height
					&&  clipRect.z >= 0.0f
					&&  clipRect.w >= 0.0f)
					{
						const uint16_t xx = uint16_t(bx::max(clipRect.x, 0.0f) );
						const uint16_t yy = uint16_t(bx::max(clipRect.y, 0.0f) );
						encoder->setScissor(xx, yy
								, uint16_t(bx::min(clipRect.z, 65535.0f)-xx)
								, uint16_t(bx::min(clipRect.w, 65535.0f)-yy)
								);

						encoder->setState(state);
						encoder->setTexture(0, s_tex, th);
						encoder->setVertexBuffer(0, &tvb, cmd->VtxOffset, numVertices);
						encoder->setIndexBuffer(&tib, cmd->IdxOffset, cmd->ElemCount);
						encoder->submit(m_viewId, program);
					}
				}
			}

			bgfx::end(encoder);
		}
	}

	void create(float _fontSize, bx::AllocatorI* _allocator)
	{
		m_allocator = _allocator;

		if (NULL == _allocator)
		{
			static bx::DefaultAllocator allocator;
			m_allocator = &allocator;
		}

		m_viewId = 255;
		m_lastScroll = 0;
		m_last = bx::getHPCounter();

		ImGui::SetAllocatorFunctions(memAlloc, memFree, NULL);

		m_imgui = ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();

		io.DisplaySize = ImVec2(1280.0f, 720.0f);
		io.DeltaTime   = 1.0f / 60.0f;
		io.IniFilename = NULL;

		setupStyle(true);

		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

#if USE_ENTRY
		for (int32_t ii = 0; ii < (int32_t)entry::Key::Count; ++ii)
		{
			m_keyMap[ii] = ImGuiKey_None;
		}

		m_keyMap[entry::Key::Esc]          = ImGuiKey_Escape;
		m_keyMap[entry::Key::Return]       = ImGuiKey_Enter;
		m_keyMap[entry::Key::Tab]          = ImGuiKey_Tab;
		m_keyMap[entry::Key::Space]        = ImGuiKey_Space;
		m_keyMap[entry::Key::Backspace]    = ImGuiKey_Backspace;
		m_keyMap[entry::Key::Up]           = ImGuiKey_UpArrow;
		m_keyMap[entry::Key::Down]         = ImGuiKey_DownArrow;
		m_keyMap[entry::Key::Left]         = ImGuiKey_LeftArrow;
		m_keyMap[entry::Key::Right]        = ImGuiKey_RightArrow;
		m_keyMap[entry::Key::Insert]       = ImGuiKey_Insert;
		m_keyMap[entry::Key::Delete]       = ImGuiKey_Delete;
		m_keyMap[entry::Key::Home]         = ImGuiKey_Home;
		m_keyMap[entry::Key::End]          = ImGuiKey_End;
		m_keyMap[entry::Key::PageUp]       = ImGuiKey_PageUp;
		m_keyMap[entry::Key::PageDown]     = ImGuiKey_PageDown;
		m_keyMap[entry::Key::Print]        = ImGuiKey_PrintScreen;
		m_keyMap[entry::Key::Plus]         = ImGuiKey_Equal;
		m_keyMap[entry::Key::Minus]        = ImGuiKey_Minus;
		m_keyMap[entry::Key::LeftBracket]  = ImGuiKey_LeftBracket;
		m_keyMap[entry::Key::RightBracket] = ImGuiKey_RightBracket;
		m_keyMap[entry::Key::Semicolon]    = ImGuiKey_Semicolon;
		m_keyMap[entry::Key::Quote]        = ImGuiKey_Apostrophe;
		m_keyMap[entry::Key::Comma]        = ImGuiKey_Comma;
		m_keyMap[entry::Key::Period]       = ImGuiKey_Period;
		m_keyMap[entry::Key::Slash]        = ImGuiKey_Slash;
		m_keyMap[entry::Key::Backslash]    = ImGuiKey_Backslash;
		m_keyMap[entry::Key::Tilde]        = ImGuiKey_GraveAccent;
		m_keyMap[entry::Key::F1]           = ImGuiKey_F1;
		m_keyMap[entry::Key::F2]           = ImGuiKey_F2;
		m_keyMap[entry::Key::F3]           = ImGuiKey_F3;
		m_keyMap[entry::Key::F4]           = ImGuiKey_F4;
		m_keyMap[entry::Key::F5]           = ImGuiKey_F5;
		m_keyMap[entry::Key::F6]           = ImGuiKey_F6;
		m_keyMap[entry::Key::F7]           = ImGuiKey_F7;
		m_keyMap[entry::Key::F8]           = ImGuiKey_F8;
		m_keyMap[entry::Key::F9]           = ImGuiKey_F9;
		m_keyMap[entry::Key::F10]          = ImGuiKey_F10;
		m_keyMap[entry::Key::F11]          = ImGuiKey_F11;
		m_keyMap[entry::Key::F12]          = ImGuiKey_F12;
		m_keyMap[entry::Key::NumPad0]      = ImGuiKey_Keypad0;
		m_keyMap[entry::Key::NumPad1]      = ImGuiKey_Keypad1;
		m_keyMap[entry::Key::NumPad2]      = ImGuiKey_Keypad2;
		m_keyMap[entry::Key::NumPad3]      = ImGuiKey_Keypad3;
		m_keyMap[entry::Key::NumPad4]      = ImGuiKey_Keypad4;
		m_keyMap[entry::Key::NumPad5]      = ImGuiKey_Keypad5;
		m_keyMap[entry::Key::NumPad6]      = ImGuiKey_Keypad6;
		m_keyMap[entry::Key::NumPad7]      = ImGuiKey_Keypad7;
		m_keyMap[entry::Key::NumPad8]      = ImGuiKey_Keypad8;
		m_keyMap[entry::Key::NumPad9]      = ImGuiKey_Keypad9;
		m_keyMap[entry::Key::Key0]         = ImGuiKey_0;
		m_keyMap[entry::Key::Key1]         = ImGuiKey_1;
		m_keyMap[entry::Key::Key2]         = ImGuiKey_2;
		m_keyMap[entry::Key::Key3]         = ImGuiKey_3;
		m_keyMap[entry::Key::Key4]         = ImGuiKey_4;
		m_keyMap[entry::Key::Key5]         = ImGuiKey_5;
		m_keyMap[entry::Key::Key6]         = ImGuiKey_6;
		m_keyMap[entry::Key::Key7]         = ImGuiKey_7;
		m_keyMap[entry::Key::Key8]         = ImGuiKey_8;
		m_keyMap[entry::Key::Key9]         = ImGuiKey_9;
		m_keyMap[entry::Key::KeyA]         = ImGuiKey_A;
		m_keyMap[entry::Key::KeyB]         = ImGuiKey_B;
		m_keyMap[entry::Key::KeyC]         = ImGuiKey_C;
		m_keyMap[entry::Key::KeyD]         = ImGuiKey_D;
		m_keyMap[entry::Key::KeyE]         = ImGuiKey_E;
		m_keyMap[entry::Key::KeyF]         = ImGuiKey_F;
		m_keyMap[entry::Key::KeyG]         = ImGuiKey_G;
		m_keyMap[entry::Key::KeyH]         = ImGuiKey_H;
		m_keyMap[entry::Key::KeyI]         = ImGuiKey_I;
		m_keyMap[entry::Key::KeyJ]         = ImGuiKey_J;
		m_keyMap[entry::Key::KeyK]         = ImGuiKey_K;
		m_keyMap[entry::Key::KeyL]         = ImGuiKey_L;
		m_keyMap[entry::Key::KeyM]         = ImGuiKey_M;
		m_keyMap[entry::Key::KeyN]         = ImGuiKey_N;
		m_keyMap[entry::Key::KeyO]         = ImGuiKey_O;
		m_keyMap[entry::Key::KeyP]         = ImGuiKey_P;
		m_keyMap[entry::Key::KeyQ]         = ImGuiKey_Q;
		m_keyMap[entry::Key::KeyR]         = ImGuiKey_R;
		m_keyMap[entry::Key::KeyS]         = ImGuiKey_S;
		m_keyMap[entry::Key::KeyT]         = ImGuiKey_T;
		m_keyMap[entry::Key::KeyU]         = ImGuiKey_U;
		m_keyMap[entry::Key::KeyV]         = ImGuiKey_V;
		m_keyMap[entry::Key::KeyW]         = ImGuiKey_W;
		m_keyMap[entry::Key::KeyX]         = ImGuiKey_X;
		m_keyMap[entry::Key::KeyY]         = ImGuiKey_Y;
		m_keyMap[entry::Key::KeyZ]         = ImGuiKey_Z;

		io.ConfigFlags |= 0
			| ImGuiConfigFlags_NavEnableGamepad
			| ImGuiConfigFlags_NavEnableKeyboard
			;

		m_keyMap[entry::Key::GamepadStart]     = ImGuiKey_GamepadStart;
		m_keyMap[entry::Key::GamepadBack]      = ImGuiKey_GamepadBack;
		m_keyMap[entry::Key::GamepadY]         = ImGuiKey_GamepadFaceUp;
		m_keyMap[entry::Key::GamepadA]         = ImGuiKey_GamepadFaceDown;
		m_keyMap[entry::Key::GamepadX]         = ImGuiKey_GamepadFaceLeft;
		m_keyMap[entry::Key::GamepadB]         = ImGuiKey_GamepadFaceRight;
		m_keyMap[entry::Key::GamepadUp]        = ImGuiKey_GamepadDpadUp;
		m_keyMap[entry::Key::GamepadDown]      = ImGuiKey_GamepadDpadDown;
		m_keyMap[entry::Key::GamepadLeft]      = ImGuiKey_GamepadDpadLeft;
		m_keyMap[entry::Key::GamepadRight]     = ImGuiKey_GamepadDpadRight;
		m_keyMap[entry::Key::GamepadShoulderL] = ImGuiKey_GamepadL1;
		m_keyMap[entry::Key::GamepadShoulderR] = ImGuiKey_GamepadR1;
		m_keyMap[entry::Key::GamepadThumbL]    = ImGuiKey_GamepadL3;
		m_keyMap[entry::Key::GamepadThumbR]    = ImGuiKey_GamepadR3;
#endif // USE_ENTRY

		bgfx::RendererType::Enum type = bgfx::getRendererType();
		m_program = bgfx::createProgram(
			  bgfx::createEmbeddedShader(s_embeddedShaders, type, "vs_ocornut_imgui")
			, bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_ocornut_imgui")
			, true
			);

		u_imageLodEnabled = bgfx::createUniform("u_imageLodEnabled", bgfx::UniformType::Vec4);
		m_imageProgram = bgfx::createProgram(
			  bgfx::createEmbeddedShader(s_embeddedShaders, type, "vs_imgui_image")
			, bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_imgui_image")
			, true
			);

		m_layout
			.begin()
			.add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
			.end();

		s_tex = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

		uint8_t* data;
		int32_t width;
		int32_t height;
		{
			ImFontConfig config;
			config.FontDataOwnedByAtlas = false;
			config.MergeMode = false;
//			config.MergeGlyphCenterV = true;

			const ImWchar* ranges = io.Fonts->GetGlyphRangesCyrillic();
			m_font[ImGui::Font::Regular] = io.Fonts->AddFontFromMemoryTTF( (void*)s_robotoRegularTtf,     sizeof(s_robotoRegularTtf),     _fontSize,      &config, ranges);
			m_font[ImGui::Font::Mono   ] = io.Fonts->AddFontFromMemoryTTF( (void*)s_robotoMonoRegularTtf, sizeof(s_robotoMonoRegularTtf), _fontSize-3.0f, &config, ranges);

			config.MergeMode = true;
			config.DstFont   = m_font[ImGui::Font::Regular];

			for (uint32_t ii = 0; ii < BX_COUNTOF(s_fontRangeMerge); ++ii)
			{
				const FontRangeMerge& frm = s_fontRangeMerge[ii];

				io.Fonts->AddFontFromMemoryTTF( (void*)frm.data
						, (int)frm.size
						, _fontSize-3.0f
						, &config
						, frm.ranges
						);
			}
		}

		io.Fonts->GetTexDataAsRGBA32(&data, &width, &height);

		m_texture = bgfx::createTexture2D(
			  (uint16_t)width
			, (uint16_t)height
			, false
			, 1
			, bgfx::TextureFormat::BGRA8
			, 0
			, bgfx::copy(data, width*height*4)
			);

		ImGui::InitDockContext();
	}

	void destroy()
	{
		ImGui::ShutdownDockContext();
		ImGui::DestroyContext(m_imgui);

		bgfx::destroy(s_tex);
		bgfx::destroy(m_texture);

		bgfx::destroy(u_imageLodEnabled);
		bgfx::destroy(m_imageProgram);
		bgfx::destroy(m_program);

		m_allocator = NULL;
	}

	void setupStyle(bool _dark)
	{
		// Doug Binks' darl color scheme
		// https://gist.github.com/dougbinks/8089b4bbaccaaf6fa204236978d165a9
		ImGuiStyle& style = ImGui::GetStyle();
		if (_dark)
		{
			ImGui::StyleColorsDark(&style);
		}
		else
		{
			ImGui::StyleColorsLight(&style);
		}

		style.FrameRounding    = 4.0f;
		style.WindowBorderSize = 0.0f;
	}

	void beginFrame(
		  int32_t _mx
		, int32_t _my
		, uint8_t _button
		, int32_t _scroll
		, int _width
		, int _height
		, int _inputChar
		, bgfx::ViewId _viewId
		)
	{
		m_viewId = _viewId;

		ImGuiIO& io = ImGui::GetIO();
		if (_inputChar >= 0)
		{
			io.AddInputCharacter(_inputChar);
		}

		io.DisplaySize = ImVec2( (float)_width, (float)_height);

		const int64_t now = bx::getHPCounter();
		const int64_t frameTime = now - m_last;
		m_last = now;
		const double freq = double(bx::getHPFrequency() );
		io.DeltaTime = float(frameTime/freq);

		io.AddMousePosEvent( (float)_mx, (float)_my);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left,   0 != (_button & IMGUI_MBUT_LEFT  ) );
		io.AddMouseButtonEvent(ImGuiMouseButton_Right,  0 != (_button & IMGUI_MBUT_RIGHT ) );
		io.AddMouseButtonEvent(ImGuiMouseButton_Middle, 0 != (_button & IMGUI_MBUT_MIDDLE) );
		io.AddMouseWheelEvent(0.0f, (float)(_scroll - m_lastScroll) );
		m_lastScroll = _scroll;

#if USE_ENTRY
		uint8_t modifiers = inputGetModifiersState();
		io.AddKeyEvent(ImGuiKey_ModShift, 0 != (modifiers & (entry::Modifier::LeftShift | entry::Modifier::RightShift) ) );
		io.AddKeyEvent(ImGuiKey_ModCtrl,  0 != (modifiers & (entry::Modifier::LeftCtrl  | entry::Modifier::RightCtrl ) ) );
		io.AddKeyEvent(ImGuiKey_ModAlt,   0 != (modifiers & (entry::Modifier::LeftAlt   | entry::Modifier::RightAlt  ) ) );
		io.AddKeyEvent(ImGuiKey_ModSuper, 0 != (modifiers & (entry::Modifier::LeftMeta  | entry::Modifier::RightMeta ) ) );
		for (int32_t ii = 0; ii < (int32_t)entry::Key::Count; ++ii)
		{
			io.AddKeyEvent(m_keyMap[ii], inputGetKeyState(entry::Key::Enum(ii) ) );
			io.SetKeyEventNativeData(m_keyMap[ii], 0, 0, ii);
		}
#endif // USE_ENTRY

		ImGui::NewFrame();

		ImGuizmo::BeginFrame();
	}

	void endFrame()
	{
		ImGui::Render();
		render(ImGui::GetDrawData() );
	}

	ImGuiContext*       m_imgui;
	bx::AllocatorI*     m_allocator;
	bgfx::VertexLayout  m_layout;
	bgfx::ProgramHandle m_program;
	bgfx::ProgramHandle m_imageProgram;
	bgfx::TextureHandle m_texture;
	bgfx::UniformHandle s_tex;
	bgfx::UniformHandle u_imageLodEnabled;
	ImFont* m_font[ImGui::Font::Count];
	int64_t m_last;
	int32_t m_lastScroll;
	bgfx::ViewId m_viewId;
#if USE_ENTRY
	ImGuiKey m_keyMap[(int)entry::Key::Count];
#endif // USE_ENTRY
};

static OcornutImguiContext s_ctx;

static void* memAlloc(size_t _size, void* _userData)
{
	BX_UNUSED(_userData);
	return BX_ALLOC(s_ctx.m_allocator, _size);
}

static void memFree(void* _ptr, void* _userData)
{
	BX_UNUSED(_userData);
	BX_FREE(s_ctx.m_allocator, _ptr);
}

void imguiCreate(float _fontSize, bx::AllocatorI* _allocator)
{
	s_ctx.create(_fontSize, _allocator);
}

void imguiDestroy()
{
	s_ctx.destroy();
}

void imguiBeginFrame(int32_t _mx, int32_t _my, uint8_t _button, int32_t _scroll, uint16_t _width, uint16_t _height, int _inputChar, bgfx::ViewId _viewId)
{
	s_ctx.beginFrame(_mx, _my, _button, _scroll, _width, _height, _inputChar, _viewId);
}

void imguiEndFrame()
{
	s_ctx.endFrame();
}

namespace ImGui
{
	void PushFont(Font::Enum _font)
	{
		PushFont(s_ctx.m_font[_font]);
	}

	void PushEnabled(bool _enabled)
	{
		extern void PushItemFlag(int option, bool enabled);
		PushItemFlag(ImGuiItemFlags_Disabled, !_enabled);
		PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (_enabled ? 1.0f : 0.5f) );
	}

	void PopEnabled()
	{
		extern void PopItemFlag();
		PopItemFlag();
		PopStyleVar();
	}

} // namespace ImGui

BX_PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4505); // error C4505: '' : unreferenced local function has been removed
BX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wunused-function"); // warning: 'int rect_width_compare(const void*, const void*)' defined but not used
BX_PRAGMA_DIAGNOSTIC_PUSH();
BX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG("-Wunknown-pragmas")
BX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wtype-limits"); // warning: comparison is always true due to limited range of data type
#define STBTT_malloc(_size, _userData) memAlloc(_size, _userData)
#define STBTT_free(_ptr, _userData) memFree(_ptr, _userData)
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb/stb_rect_pack.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>
BX_PRAGMA_DIAGNOSTIC_POP();

#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void update_texture(ApplicationState& ctx, const u8* render_data) {
    const FlUpdateTexture* cmd = (FlUpdateTexture*)render_data;

    const Texture* texture = &ctx.textures[cmd->texture_id];
    const bgfx::Memory* mem = bgfx::makeRef(cmd->data, texture->size);
    const FlRenderRect* rect = &cmd->rect;

    bgfx::updateTexture2D(texture->handle, 0, 0, rect->x0, rect->y0, rect->x1 - rect->x0, rect->y1 - rect->y0, mem,
                          texture->width);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void render_flowi(ApplicationState& state, uint16_t width, uint16_t height) {
    bgfx::setViewMode(0, bgfx::ViewMode::Sequential);

    //FlStyle style = fl_style_get_current(state.ctx);

    //bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, (style.background_color << 8) | 0xff, 1.0f, 0);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, (0x112233 << 8) | 0xff, 1.0f, 0);

    int view_id = 255;

    bgfx::setViewName(view_id, "Flowi");
    bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);

    float ortho[16];

    bx::mtxOrtho(ortho, 0.0f, float(width), float(height), 0.0f, 0.0f, 1000.0f, 0.0f, 1.0f);
    bgfx::setViewTransform(view_id, NULL, ortho);
    bgfx::setViewRect(view_id, 0, 0, width, height);

    const int count = fl_render_begin_commands(state.flowi_state);
    const u8* render_cmd_data = nullptr;
    u16 cmd = 0;

    bgfx::Encoder* encoder = bgfx::begin();

    // process all the render commands
    for (int i = 0; i < count; ++i) {
        switch (cmd = fl_render_get_command(state.flowi_state, &render_cmd_data)) {
            case FlRenderCommand_TexturedTriangles: {
                render_textured_triangles(state, render_cmd_data, encoder/*, style*/);
                break;
            }

            case FlRenderCommand_SolidTriangles: {
                render_flat_triangles(state, render_cmd_data, encoder/*, style*/);
                break;
            }

            case FlRenderCommand_CreateTexture: {
                create_texture(state, render_cmd_data);
                break;
            }

            case FlRenderCommand_UpdateTexture: {
                update_texture(state, render_cmd_data);
                break;
            }

            default: {
                printf("Case %d - not handled!\n", cmd);
                break;
            }
        }
    }

    bgfx::end(encoder);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void generate_frame(void* user_data) {
    ApplicationState* state = (ApplicationState*)user_data;

    int display_w, display_h;
    glfwGetWindowSize(state->default_window, &display_w, &display_h);

    const int reset_flags = BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X8;

    if ((state->window_width != display_w) || (state->window_height != display_h) || state->counter != 0) {
        bgfx::reset(display_w, display_h, reset_flags);
        state->window_width = display_w;
        state->window_height = display_h;
        state->counter--;
    }

    bgfx::setViewRect(0, 0, 0, uint16_t(display_w), uint16_t(display_h));

    // This dummy draw call is here to make sure that view 0 is cleared
    // if no other draw calls are submitted to view 0.
    bgfx::touch(0);

    double xpos, ypos;
    glfwGetCursorPos(state->default_window, &xpos, &ypos);
    //const int mouse_state = glfwGetMouseButton(state->default_window, GLFW_MOUSE_BUTTON_LEFT);
    //FlVec2 pos = {float(xpos), float(ypos)};

    //fl_set_mouse_pos_state(state->ctx, pos, mouse_state == GLFW_PRESS ? true : false, false, false);

    // TODO: Correct delta time.
    fl_frame_begin(state->ctx, display_w, display_h, 1.0f/60.0f);

    if (state->main_callback) {
        state->main_callback(state->ctx, state->user_data);
    }

    fl_frame_end(state->ctx);
    render_flowi(*state, display_w, display_h);

    bgfx::frame();

    glfwPollEvents();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void fl_application_main_loop_impl(FlMainLoopCallback callback, void* user_data) {
    ApplicationState* state = &s_state;

    state->main_callback = callback;
    state->user_data = user_data;
    state->counter = 2;

    // Run the loop correctly for the target environment
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(generate_frame, state, 0, false);
#else
    // Display the window until ESC is pressed
    while (!glfwWindowShouldClose(state->default_window)) {
        generate_frame((void*)state);
    }

    bgfx::shutdown();

    // Clean up
    glfwDestroyWindow(state->default_window);
    glfwTerminate();
#endif
}
