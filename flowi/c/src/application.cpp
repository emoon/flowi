#include <bgfx/embedded_shader.h>
#include <flowi/application.h>
#include "../shaders/generated/color_fill.h"
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <flowi_core/font.h>

// TODO: Should be in public core api
#include "../../../core/c/src/flowi.h"
#include "../../../core/c/src/area.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const bgfx::EmbeddedShader s_shaders[] = {BGFX_EMBEDDED_SHADER(color_fill_vs),
                                                 BGFX_EMBEDDED_SHADER(color_fill_fs), BGFX_EMBEDDED_SHADER_END()};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Texture {
    bgfx::TextureHandle handle;
    int size;
    int width;
    int height;
    float inv_x;
    float inv_y;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define MAX_TEXTURE_COUNT 128
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

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
    //
    bgfx::UniformHandle tex_handle;
    bgfx::UniformHandle u_inv_res_tex;

    GLFWwindow* default_window;

    FlMainLoopCallback main_callback;
    void* user_data;
};

static ApplicationState s_state = {0};

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
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void error_callback(int error, const char* description) {
    fprintf(stderr, "Error: %s\n", description);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern"C" struct FlContext* fl_application_new_impl(struct FlContext* ctx, FlString application_name, FlString developer) {
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
    glfwWindowHint(GLFW_FLOATING, GL_TRUE);

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
    bgfxInit.type = bgfx::RendererType::Count;
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

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x001f001f, 1.0f, 0);

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

    state->flat_shader = bgfx::createProgram(
        bgfx::createEmbeddedShader(s_shaders, type, "color_fill_vs"),
        bgfx::createEmbeddedShader(s_shaders, type, "color_fill_fs"));

    if (!bgfx::isValid(state->flat_shader)) {
        printf("failed to init flat_shader shaders\n");
        return NULL;
    }

    state->u_inv_res_tex = bgfx::createUniform("u_inv_res_tex", bgfx::UniformType::Vec4);

    return state->ctx;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render triangles without texture

static void render_textured_triangles(ApplicationState& ctx, const u8* render_data, bgfx::Encoder* encoder) {
    FlTexturedTriangles* draw_cmd = (FlTexturedTriangles*)render_data;

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;

    const int vertex_count = draw_cmd->vertex_buffer_size;
    const int index_count = draw_cmd->index_buffer_size;
    const u32 texture_id = draw_cmd->texture_id;

    const Texture& texture = ctx.textures[texture_id];

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
    encoder->submit(255, ctx.texture_shader);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render triangles without texture

static void render_flat_triangles(ApplicationState& ctx, const u8* render_data, bgfx::Encoder* encoder) {
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
        case FlTextureFormat_R8_LINEAR: {
            const bgfx::Memory* mem = nullptr;

            if (data) {
                mem = bgfx::makeRef(data, width * height);
            }

            Texture* texture = &ctx.textures[id];
            texture->handle = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::R8, 0, mem);
            texture->inv_x = 1.0f / width;
            texture->inv_y = 1.0f / height;
            texture->size = width * height;
            texture->height = height;
            texture->width = width;
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

    bgfx::updateTexture2D(texture->handle, 0, 0, rect->x0, rect->y0, rect->x1 - rect->x0, rect->y1 - rect->y0, mem,
                          texture->width);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void render_flowi(ApplicationState& state, uint16_t width, uint16_t height) {
    bgfx::setViewMode(0, bgfx::ViewMode::Sequential);

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
                render_textured_triangles(state, render_cmd_data, encoder);
                break;
            }

            case FlRenderCommand_SolidTriangles: {
                render_flat_triangles(state, render_cmd_data, encoder);
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

    fl_frame_begin(state->ctx);

    if (state->main_callback) {
        state->main_callback(state->ctx, state->user_data);
    }

    Area_generate_circle(state->ctx);
    //fl_text(state->ctx, "Testing");

    fl_frame_end(state->ctx);
    render_flowi(*state, display_w, display_h);

    bgfx::frame();

    glfwPollEvents();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void fl_application_main_loop_impl(struct FlContext* ctx, FlMainLoopCallback callback, void* user_data) {
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
