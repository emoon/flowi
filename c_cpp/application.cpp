// clang-format off
#include <assert.h>
#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include <flowi/application.h>
#include <flowi/style.h>
#include <flowi/flowi.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include "../shaders/generated/color_fill.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "imgui_impl_glfw.h"
#include "render.h"
#include "image_private.h"
#include "handles.h"
#include "internal.h"
// clang-format off

// TODO: Should be in public core api
//#include "../../../core/c/src/area.h"
#include "flowi_internal.h"

#include "vs_ocornut_imgui.bin.h"
#include "fs_ocornut_imgui.bin.h"
#include "vs_imgui_image.bin.h"
#include "fs_imgui_image.bin.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const bgfx::EmbeddedShader s_dear_imgui_shaders[] = {
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(vs_imgui_image),
    BGFX_EMBEDDED_SHADER(fs_imgui_image),
    BGFX_EMBEDDED_SHADER_END()
};

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

struct DearImguiState {
    bgfx::VertexLayout layout;
    bgfx::UniformHandle tex_handle;
	bgfx::ProgramHandle program;
	bgfx::ProgramHandle image_program;
    bgfx::UniformHandle u_image_lod_enabled;
	bgfx::UniformHandle tex;
	bgfx::TextureHandle texture;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct ApplicationState {
    struct FlGlobalState* flowi_state;
    struct FlContext* ctx;
    struct FlInternalData* data;
    int window_width;
    int window_height;
    int counter;

    // Dear Imgui data
    DearImguiState dear_imgui;

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

// TODO: We really shouldn't have any global state
static ApplicationState s_state;
static FlApplication s_app;

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

static bool application_main_loop(FlMainLoopCallback callback, void* user_data);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlIoApi* get_io_api(FlInternalData* data, int version) {
    FL_UNUSED(version);
    return &data->io_funcs;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" struct FlApplication* fl_application_create_impl(FlApplicationSettings* settings) {
    FL_UNUSED(settings);

    ApplicationState* state = &s_state;

    // TODO: Error, we only support one application so make sure we only run this once.
    if (state->ctx != nullptr) {
        printf("Application already created\n");
        return nullptr;
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
        return nullptr;
    }

    state->ctx = fl_context_create(state->flowi_state);
    state->data = state->ctx->priv;

    ImGui::CreateContext();

    s_app.main_loop = application_main_loop;
    s_app.io_get_api = get_io_api;
    s_app.priv = state->data;

    return &s_app; 
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void render_dear_imgui(const ApplicationState& app_state, const DearImguiState& imgui_data, ImDrawData* draw_data) {
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0)
        return;


    int view_id = 255;

    bgfx::setViewName(view_id, "ImGui");
    bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);

    const bgfx::Caps* caps = bgfx::getCaps();
    {
        float ortho[16];
        float x = draw_data->DisplayPos.x;
        float y = draw_data->DisplayPos.y;
        float width = draw_data->DisplaySize.x;
        float height = draw_data->DisplaySize.y;

        bx::mtxOrtho(ortho, x, x + width, y + height, y, 0.0f, 1000.0f, 0.0f, caps->homogeneousDepth);
        bgfx::setViewTransform(view_id, NULL, ortho);
        bgfx::setViewRect(view_id, 0, 0, uint16_t(width), uint16_t(height));
    }

    const ImVec2 clip_pos = draw_data->DisplayPos;       // (0,0) unless using multi-viewports
    const ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    // Render command lists
    for (int32_t ii = 0, num = draw_data->CmdListsCount; ii < num; ++ii) {
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;

        const ImDrawList* draw_list = draw_data->CmdLists[ii];
        uint32_t vertex_count = (uint32_t)draw_list->VtxBuffer.size();
        uint32_t index_count  = (uint32_t)draw_list->IdxBuffer.size();

        bgfx::allocTransientVertexBuffer(&tvb, vertex_count, imgui_data.layout);
        bgfx::allocTransientIndexBuffer(&tib, index_count, sizeof(ImDrawIdx) == 4);

        ImDrawVert* verts = (ImDrawVert*)tvb.data;
        bx::memCopy(verts, draw_list->VtxBuffer.begin(), vertex_count * sizeof(ImDrawVert) );

        ImDrawIdx* indices = (ImDrawIdx*)tib.data;
        bx::memCopy(indices, draw_list->IdxBuffer.begin(), index_count * sizeof(ImDrawIdx) );

        bgfx::Encoder* encoder = bgfx::begin();

        for (const ImDrawCmd* cmd = draw_list->CmdBuffer.begin(), *cmd_end = draw_list->CmdBuffer.end(); cmd != cmd_end; ++cmd) {
            if (cmd->ElemCount == 0) {
                continue;
            }

            uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;

            bgfx::TextureHandle th = imgui_data.texture;
            bgfx::ProgramHandle program = imgui_data.program;

            if (cmd->TextureId != 0) {
                ImagePrivate* image_data = (ImagePrivate*)Handles_get_data(&app_state.data->global->image_handles, cmd->TextureId);

                if (!image_data) {
                    //ERROR_ADD(FlError_Image, "Invalid handle %s", "todo name");
                    continue;
                }

                th = app_state.textures[image_data->texture_id].handle;
                program = imgui_data.image_program;
            } else {
                state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
            }

            // Project scissor/clipping rectangles into framebuffer space
            ImVec4 clip_rect;
            clip_rect.x = (cmd->ClipRect.x - clip_pos.x) * clip_scale.x;
            clip_rect.y = (cmd->ClipRect.y - clip_pos.y) * clip_scale.y;
            clip_rect.z = (cmd->ClipRect.z - clip_pos.x) * clip_scale.x;
            clip_rect.w = (cmd->ClipRect.w - clip_pos.y) * clip_scale.y;

            if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f) {
                const uint16_t xx = uint16_t(bx::max(clip_rect.x, 0.0f) );
                const uint16_t yy = uint16_t(bx::max(clip_rect.y, 0.0f) );
                encoder->setScissor(xx, yy, 
                    uint16_t(bx::min(clip_rect.z, 65535.0f)-xx), 
                    uint16_t(bx::min(clip_rect.w, 65535.0f)-yy));
                encoder->setState(state);
                encoder->setTexture(0, imgui_data.tex, th);
                encoder->setVertexBuffer(0, &tvb, cmd->VtxOffset, vertex_count);
                encoder->setIndexBuffer(&tib, cmd->IdxOffset, cmd->ElemCount);
                encoder->submit(view_id, program);
            }
        }

        bgfx::end(encoder);
    }
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render triangles without texture
//

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
    bgfx::allocTransientIndexBuffer(&tib, index_count, sizeof(u16) == 4);

    void* verts = (void*)tvb.data;
    memcpy(verts, draw_cmd->vertex_buffer, vertex_count * sizeof(FlVertPosUvColor));

    u16* indices = (u16*)tib.data;
    memcpy(indices, draw_cmd->index_buffer, index_count * sizeof(u16));

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
    bgfx::allocTransientIndexBuffer(&tib, index_count, sizeof(u16) == 4);

    void* verts = (void*)tvb.data;
    memcpy(verts, draw_cmd->vertex_buffer, vertex_count * sizeof(FlVertPosColor));

    u16* indices = (u16*)tib.data;
    memcpy(indices, draw_cmd->index_buffer, index_count * sizeof(u16));

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

            printf("Creating texture %d %d %d\n", width, height, id);

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void update_texture(ApplicationState& ctx, const u8* render_data) {
    const FlUpdateTexture* cmd = (FlUpdateTexture*)render_data;

    const Texture* texture = &ctx.textures[cmd->texture_id];
    const bgfx::Memory* mem = bgfx::makeRef(cmd->data, texture->size);
    const FlRenderRect* rect = &cmd->rect;

    printf("update texture %d - %d %d %d %d\n", cmd->texture_id, rect->x0, rect->y0, rect->x1, rect->y1);

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
    
    ImGui::StyleColorsDark();

    int display_w, display_h;
    glfwGetWindowSize(state->default_window, &display_w, &display_h);

    //const int reset_flags = BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X8;
    const int reset_flags = BGFX_RESET_VSYNC;

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
    //fl_frame_begin(state->data, display_w, display_h, 1.0f/60.0f);
    fl_frame_begin(state->data, display_w, display_h, 1.0f/144.0f);
    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();

    if (state->main_callback) {
        state->main_callback(state->ctx, state->user_data);
    }

    fl_frame_end(state->data);
    ImGui::Render();

    render_flowi(*state, display_w, display_h);
    render_dear_imgui(*state, state->dear_imgui, ImGui::GetDrawData());

    bgfx::frame();

    glfwPollEvents();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool application_main_loop(FlMainLoopCallback callback, void* user_data) {
    ApplicationState* state = &s_state;

    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        // TODO: Proper error
        printf("failed to init glfw\n");
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_FLOATING, GL_FALSE);

    state->default_window = glfwCreateWindow(state->window_width, state->window_height, "Fix me title", NULL, NULL);
    if (!state->default_window) {
        printf("failed to open window\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(state->default_window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    //io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = NULL;

    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
    colors[ImGuiCol_Border]                 = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_Button]                 = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
    colors[ImGuiCol_Separator]              = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_DockingPreview]         = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_DockingEmptyBg]         = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding                     = ImVec2(8.00f, 8.00f);
    style.FramePadding                      = ImVec2(5.00f, 2.00f);
    style.CellPadding                       = ImVec2(6.00f, 6.00f);
    style.ItemSpacing                       = ImVec2(6.00f, 6.00f);
    style.ItemInnerSpacing                  = ImVec2(6.00f, 6.00f);
    style.TouchExtraPadding                 = ImVec2(0.00f, 0.00f);
    style.IndentSpacing                     = 25;
    style.ScrollbarSize                     = 15;
    style.GrabMinSize                       = 10;
    style.WindowBorderSize                  = 1;
    style.ChildBorderSize                   = 1;
    style.PopupBorderSize                   = 1;
    style.FrameBorderSize                   = 1;
    style.TabBorderSize                     = 1;
    style.WindowRounding                    = 7;
    style.ChildRounding                     = 4;
    style.FrameRounding                     = 3;
    style.PopupRounding                     = 4;
    style.ScrollbarRounding                 = 9;
    style.GrabRounding                      = 3;
    style.LogSliderDeadzone                 = 4;
    style.TabRounding                       = 4;

    // Setup Dear ImGui style
    //ImGui::StyleColorsDark();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOther(state->default_window, true);

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
        return false;
    }

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x2f2f2fff, 1.0f, 0);

    bgfx::RendererType::Enum type = bgfx::getRendererType();

    state->dear_imgui.program = bgfx::createProgram(
            bgfx::createEmbeddedShader(s_dear_imgui_shaders, type, "vs_ocornut_imgui"), 
            bgfx::createEmbeddedShader(s_dear_imgui_shaders, type, "fs_ocornut_imgui"), true);

    state->dear_imgui.u_image_lod_enabled = bgfx::createUniform("u_imageLodEnabled", bgfx::UniformType::Vec4);

    state->dear_imgui.image_program = bgfx::createProgram(
            bgfx::createEmbeddedShader(s_dear_imgui_shaders, type, "vs_imgui_image"), 
            bgfx::createEmbeddedShader(s_dear_imgui_shaders, type, "fs_imgui_image"), true);

    state->dear_imgui.layout
        .begin()
        .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
        .end();

    state->dear_imgui.tex = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

    //io.Fonts->AddFontFromFileTTF("../../../data/montserrat-regular.ttf", 24.0f);
    //io.Fonts->AddFontFromFileTTF("../../../data/Montserrat-Bold.ttf", 24.0f);
    io.Fonts->Build();

    uint8_t* data;
    int32_t width;
    int32_t height;
    io.Fonts->GetTexDataAsRGBA32(&data, &width, &height);

    state->dear_imgui.texture = bgfx::createTexture2D(
          (uint16_t)width, (uint16_t)height, false, 1, bgfx::TextureFormat::BGRA8, 0, bgfx::copy(data, width*height*4));


    /*
    state->flat_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    state->texture_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Int16, false, true)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

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

    imguiCreate();
    */

    // void imguiBeginFrame(int32_t _mx, int32_t _my, uint8_t _button, int32_t _scroll, uint16_t _width, uint16_t _height, int _inputChar = -1, bgfx::ViewId _view = 255);
    //imguiEndFrame();

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

    return true;
}
