#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include "../../core/src/flowi.h"
#include "../../core/src/font.h"
#include "../../core/src/area.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_WIDTH 640 * 2
#define WINDOW_HEIGHT 400 * 2
#define MAX_TEXTURE_COUNT 128

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Texture {
    bgfx::TextureHandle handle;
    float inv_x;
    float inv_y;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Track data needed by the Flowi rendering
struct RenderContext {
    // TODO: Don't hard code
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
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void error_callback(int error, const char* description) {
    fprintf(stderr, "Error: %s\n", description);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* load_shader_file(const char* filename, int* size) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        printf("Unable to open %s\n", filename);
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    int len = (int)ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* data = (u8*)malloc(len + 1);
    fread(data, len, 1, f);
    data[len] = 0;
    fclose(f);
    *size = len;
    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bgfx::ProgramHandle load_shader_program(const char* vs, const char* fs) {
    int vs_data_len = 0;
    int fs_data_len = 0;

    void* vs_data = load_shader_file(vs, &vs_data_len);
    void* ps_data = load_shader_file(fs, &fs_data_len);

    if (!vs_data || !ps_data) {
        exit(1);
    }

    auto vs_data_mem = bgfx::copy(vs_data, vs_data_len);
    auto ps_data_mem = bgfx::copy(ps_data, fs_data_len);

    auto vs_shader = bgfx::createShader(vs_data_mem);
    auto ps_shader = bgfx::createShader(ps_data_mem);

    auto t = bgfx::createProgram(vs_shader, ps_shader, false);
    if (!bgfx::isValid(t)) {
        printf("failed to init shaders\n");
    }

    return t;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* glfwNativeWindowHandle(GLFWwindow* _window) {
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD

    return (void*)(uintptr_t)glfwGetX11Window(_window);
#elif BX_PLATFORM_OSX
    return glfwGetCocoaWindow(_window);
#elif BX_PLATFORM_WINDOWS
    return glfwGetWin32Window(_window);
#endif  // BX_PLATFORM_
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_init(RenderContext& ctx) {
    ctx.flat_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    ctx.texture_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Int16, false, true)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    ctx.u_inv_res_tex = bgfx::createUniform("u_inv_res_tex", bgfx::UniformType::Vec4);

    ctx.flat_shader = load_shader_program("t2-output/linux-gcc-debug-default/_generated/testbed/shaders/color_fill.vs",
                                          "t2-output/linux-gcc-debug-default/_generated/testbed/shaders/color_fill.fs");
    ctx.texture_shader =
        load_shader_program("t2-output/linux-gcc-debug-default/_generated/testbed/shaders/vs_texture.vs",
                            "t2-output/linux-gcc-debug-default/_generated/testbed/shaders/fs_texture.fs");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_update(FlContext* ctx) {
    fl_frame_begin(ctx);

    Area_generate_circle(ctx);

    //fl_text(ctx, "Almost before we knew it");

    /*
    if (fl_button_c(ctx, "test")) {
        printf("button\n");
    }
    */

    fl_frame_end(ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render triangles without texture

static const u8* render_textured_triangles(RenderContext& ctx, const u8* render_data, bgfx::Encoder* encoder) {
    FlRcTexturedTriangles* draw_cmd = (FlRcTexturedTriangles*)render_data;

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;

    const int vertex_count = draw_cmd->vertex_count;
    const int index_count = draw_cmd->index_count;
    const u32 texture_id = draw_cmd->texture_id;

    const Texture& texture = ctx.textures[texture_id];

    bgfx::allocTransientVertexBuffer(&tvb, vertex_count, ctx.texture_layout);
    bgfx::allocTransientIndexBuffer(&tib, index_count, sizeof(FlIdxSize) == 4);

    void* verts = (void*)tvb.data;
    memcpy(verts, draw_cmd->vertex_buffer, vertex_count * sizeof(FlVertPosUvColor));

    u16* indices = (u16*)tib.data;
    memcpy(indices, draw_cmd->index_buffer, index_count * sizeof(FlIdxSize));

    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;

    // Set 1/texture size for shader
    float data[4] = { texture.inv_x, texture.inv_y, 0.0f, 0.0f };
    encoder->setUniform(ctx.u_inv_res_tex, data, UINT16_MAX);

    encoder->setState(state);
    encoder->setTexture(0, ctx.tex_handle, texture.handle);
    encoder->setVertexBuffer(0, &tvb, 0, vertex_count);
    encoder->setIndexBuffer(&tib, 0, index_count);
    encoder->submit(255, ctx.texture_shader);

    // Return next entry in the list
    return (u8*)(draw_cmd + 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render triangles without texture

static const u8* render_flat_triangles(RenderContext& ctx, const u8* render_data, bgfx::Encoder* encoder) {
    FlRcSolidTriangles* draw_cmd = (FlRcSolidTriangles*)render_data;

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;

    const int vertex_count = draw_cmd->vertex_count;
    const int index_count = draw_cmd->index_count;

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

    // Return next entry in the list
    return (u8*)(draw_cmd + 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// static const u8* create_texture(RenderContext& ctx, const u8* render_data, bgfx::Encoder* encoder) {
static const u8* create_texture(RenderContext& ctx, const u8* render_data) {
    const FlRcCreateTexture* cmd = (FlRcCreateTexture*)render_data;
    const u8* data = cmd->data;
    const u32 id = cmd->id;
    const u16 width = cmd->width;
    const u16 height = cmd->height;
    const u16 flags = 0;

    assert(id < MAX_TEXTURE_COUNT);

    switch (cmd->format) {
        case FlTextureFormat_R8_LINEAR: {
            const bgfx::Memory* mem = bgfx::makeRef(data, width * height);
            Texture* texture = &ctx.textures[id];
            texture->handle = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::R8, flags, mem);
            texture->inv_x = 1.0f / width;
            texture->inv_y = 1.0f / height;
            break;
        }

        default: {
            // TODO: Implement support
            printf("unsupported texture format %d\n", cmd->format);
            exit(0);
        }
    }

    return (u8*)(cmd + 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_render(RenderContext& render_ctx, FlContext* flowi_ctx, uint16_t width, uint16_t height) {
    FlRenderData* render_data = fl_get_render_data(flowi_ctx);

    bgfx::setViewMode(0, bgfx::ViewMode::Sequential);

    int view_id = 255;

    bgfx::setViewName(view_id, "Flowi");
    bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);

    float ortho[16];

    bx::mtxOrtho(ortho, 0.0f, float(width), float(height), 0.0f, 0.0f, 1000.0f, 0.0f, 1.0f);
    bgfx::setViewTransform(view_id, NULL, ortho);
    bgfx::setViewRect(view_id, 0, 0, width, height);

    const u8* render_commands = render_data->render_commands;
    const u8* render_cmd_data = render_data->render_data;

    bgfx::Encoder* encoder = bgfx::begin();

    // process all the render commands
    for (int i = 0, count = render_data->count; i < count; ++i) {
        FlRenderCommand cmd = (FlRenderCommand)*render_commands++;

        switch (cmd) {
            case FlRc_RenderTexturedTriangles: {
                render_cmd_data = render_textured_triangles(render_ctx, render_cmd_data, encoder);
                break;
            }

            case FlRc_RenderTriangles: {
                render_cmd_data = render_flat_triangles(render_ctx, render_cmd_data, encoder);
                break;
            }

            case FlRc_CreateTexture: {
                render_cmd_data = create_texture(render_ctx, render_cmd_data);
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

int main() {
    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
        exit(EXIT_FAILURE);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_FLOATING, GL_TRUE);
    //glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
/*
	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);

	glfwWindowHint(GLFW_DOUBLEBUFFER, 1);
	glfwWindowHint(GLFW_RED_BITS, mode->redBits);
	glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
	glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
	glfwWindowHint(GLFW_ALPHA_BITS, GLFW_DONT_CARE);
	glfwWindowHint(GLFW_DEPTH_BITS, GLFW_DONT_CARE);
	glfwWindowHint(GLFW_STENCIL_BITS, 8);
	glfwWindowHint(GLFW_REFRESH_RATE, GLFW_DONT_CARE);
*/

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Flowi Testbed", NULL, NULL);
    if (!window) {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    bgfx::PlatformData pd;
#if defined(GLFW_EXPOSE_NATIVE_X11)
    pd.ndt = glfwGetX11Display();
#endif
    pd.nwh = glfwNativeWindowHandle(window);
    pd.context = NULL;
    pd.backBuffer = NULL;
    pd.backBufferDS = NULL;

    glfwSetKeyCallback(window, key_callback);

    bgfx::setPlatformData(pd);

    int reset_flags = BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X8;

    bgfx::Init bgfxInit;
    //bgfxInit.type = bgfx::RendererType::Count;
    bgfxInit.type = bgfx::RendererType::OpenGL;
    bgfxInit.resolution.width = WINDOW_WIDTH;
    bgfxInit.resolution.height = WINDOW_HEIGHT;
    bgfxInit.resolution.reset = reset_flags;
    bgfxInit.platformData = pd;

    printf("init\n");

    if (!bgfx::init(bgfxInit)) {
        printf("failed to init bgfx\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }

    printf("init done\n");

    //bgfx::setDebug(BGFX_DEBUG_TEXT);
    //bgfx::setViewRect(0, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
    //bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00ff00ff, 1.0f, 0);

    // glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    bgfx::setViewMode(0, bgfx::ViewMode::Sequential);

    FlGlobalState* state = fl_create(NULL);

    FlContext* ctx = fl_context_create(state);

    RenderContext render_ctx = {0};

    render_ctx.tex_handle = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

    ui_init(render_ctx);

    static u16 t[] = {0, 127};
    // static u16 t[] = {97, 100};
    static FlGlyphRange font_range = {(u16*)&t, 2};

    // Load test font
    fl_font_from_file("data/montserrat-regular.ttf", 80, FlFontBuildMode_Immediate, FlFontAtlasMode_PrebildGlyphs,
                      FlFontGlyphPlacementMode_Basic, &font_range);

    printf("finished loading font");

    int old_width = 0;
    int old_height = 0;

    //bgfx::setDebug(BGFX_DEBUG_STATS);

    glfwGetWindowSize(window, &old_width, &old_height);

    // hack
    int counter = 2;

    while (!glfwWindowShouldClose(window)) {
        int display_w, display_h;
        glfwGetWindowSize(window, &display_w, &display_h);

        if ((old_width != display_w) || (old_height != display_h) || counter != 0) {
            bgfx::reset(display_w, display_h, reset_flags);
            old_width = display_w;
            old_height = display_h;
            counter--;
        }

        bgfx::setViewRect(0, 0, 0, uint16_t(display_w), uint16_t(display_h));

        // This dummy draw call is here to make sure that view 0 is cleared
        // if no other draw calls are submitted to view 0.
        bgfx::touch(0);

        // Use debug font to print information about this example.
        //bgfx::dbgTextClear();
        //bgfx::dbgTextPrintf(0, 1, 0x4f, "bgfxTemplate");
        //bgfx::dbgTextPrintf(0, 2, 0x6f, "Description: Minimal bgfx + GLFW application.");
        //bgfx::dbgTextPrintf(0, 4, 0x4f, "Press F1 to toggle bgfx stats, Esc to quit");

        ui_update(ctx);
        ui_render(render_ctx, ctx, display_w, display_h);

        bgfx::frame();

        glfwPollEvents();
    }

    bgfx::shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
