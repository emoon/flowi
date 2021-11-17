#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include "../../core/src/flowi.h"
#include <bx/math.h>

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 400

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void error_callback(int error, const char* description) {
    fprintf(stderr, "Error: %s\n", description);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* glfwNativeWindowHandle(GLFWwindow* _window) {
#   if BX_PLATFORM_LINUX || BX_PLATFORM_BSD

    return (void*)(uintptr_t)glfwGetX11Window(_window);
#   elif BX_PLATFORM_OSX
    return glfwGetCocoaWindow(_window);
#   elif BX_PLATFORM_WINDOWS
    return glfwGetWin32Window(_window);
#   endif // BX_PLATFORM_
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_update(FliContext* ctx) {
    fli_frame_begin(ctx);

    if (fli_button_c(ctx, "test")) {
        printf("button\n");
    }

    fli_frame_end(ctx);
}

// Track data needed by the Flowi rendering
struct RenderContext {
    // TODO: Don't hard code
    bx::TextureHandle texture_handles[128];
    // layout and shader for rendering non-textured triangles
    bgfx::VertexLayout m_flat_layout;
    bgfx::ProgramHandle m_flat_shader;
    // layout and shader for rendering textured triangles
    bgfx::VertexLayout m_texture_layout;
    bgfx::ProgramHandle m_texture_shader;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_render(RenderContext& render_ctx, FliContext* flowi_ctx) {
    FliRenderData* render_data = fli_get_render_data_get(flowi_ctx);

    bgfx::setViewMode(0, bgfx::ViewMode::Sequential);

    int view_id = 255;

	bgfx::setViewName(view_id, "Flowi");
    bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);

    float ortho[16];

    // TODO: Fix hard-coding
    float x = 0.0f;
    float y = 0.0f;
    float width = 640.0f;
    float height = 360.0f;

	bx::mtxOrtho(ortho, x, x + width, y + height, y, 0.0f, 1000.0f, 0.0f, 1.0f);
    bgfx::setViewTransform(view_id, NULL, ortho);
    bgfx::setViewRect(view_id, 0, 0, uint16_t(640), uint16_t(360));

    const u8* render_commands = render_data->render_commands;
    const u8* render_data = render_data->render_data;

    // process all the render commands
    for (int i, count = render_data->render_command_count; i < count; ++i) {
        FliRenderCommand cmd = (FliRenderCommand)*render_commands++;

        switch (cmd) {
            case FliRc_RenderTriangles:
            {
                render_data = render_flat_triangles(
                break;
            }

            default:
            {
                printf("Case %d - not handled!\n", cmd);
                break;
            }
        }
    }


    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;

    int num_verts = 4;
    int num_indices = draw_data->pos_color_triangle_count * 3;

    bgfx::allocTransientVertexBuffer(&tvb, num_verts, layout);
    bgfx::allocTransientIndexBuffer(&tib, num_indices, sizeof(FliIdxSize) == 4);

    void* verts = (void*)tvb.data;
    memcpy(verts, draw_data->pos_color_vertices, num_verts * sizeof(FliVertPosColor));

    u16* indices = (u16*)tib.data;
    memcpy(indices, draw_data->pos_color_indices, num_indices * sizeof(FliIdxSize));

    uint64_t state = 0
        | BGFX_STATE_WRITE_RGB
        | BGFX_STATE_WRITE_A
        | BGFX_STATE_MSAA
        ;

    bgfx::Encoder* encoder = bgfx::begin();

    encoder->setState(state);
    encoder->setVertexBuffer(0, &tvb, 0, num_verts);
    encoder->setIndexBuffer(&tib, 0, num_indices);
    encoder->submit(view_id, program);

    bgfx::end(encoder);
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

    printf("%p %d\n", vs_data, vs_data_len);
    printf("%p %d\n", ps_data, fs_data_len);

    auto vs_data_mem = bgfx::copy(vs_data, vs_data_len);
    auto ps_data_mem = bgfx::copy(ps_data, fs_data_len);

    auto vs_shader = bgfx::createShader(vs_data_mem);
    auto ps_shader = bgfx::createShader(ps_data_mem);

    auto t = bgfx::createProgram(vs_shader, ps_shader, false);
    if (!bgfx::isValid(t)) {
        printf("failed soethuo\n");
    }

    return t;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main() {
    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
        exit(EXIT_FAILURE);

#if !defined(GLFW_EXPOSE_NATIVE_X11)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif
    glfwWindowHint(GLFW_FLOATING, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

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

    bgfx::setPlatformData(pd);

    bgfx::Init bgfxInit;
    bgfxInit.type = bgfx::RendererType::Count;
    bgfxInit.resolution.width = WINDOW_WIDTH;
    bgfxInit.resolution.height = WINDOW_HEIGHT;
    bgfxInit.resolution.reset = BGFX_RESET_VSYNC;
    bgfxInit.platformData = pd;

    if (!bgfx::init(bgfxInit)) {
        printf("failed to init bgfx\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }

    bgfx::setDebug(BGFX_DEBUG_TEXT);
    bgfx::setViewRect(0, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00ff00ff, 1.0f, 0);

    glfwSetKeyCallback(window, key_callback);
    //glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    int old_width = 0;
    int old_height = 0;

    bgfx::setViewMode(0, bgfx::ViewMode::Sequential);

    FliContext* ctx = fli_context_create();

    bgfx::VertexLayout layout;

    layout
        .begin()
        .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
        .end();

    bgfx::ProgramHandle program = load_shader_program(
        "t2-output/linux-gcc-debug-default/_generated/testbed/shaders/color_fill.vs",
        "t2-output/linux-gcc-debug-default/_generated/testbed/shaders/color_fill.fs");

    //glfwGetWindowSize(window, &old_width, &old_height);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int display_w, display_h;
        glfwGetWindowSize(window, &display_w, &display_h);

        bgfx::touch(0);

        if ((old_width != display_w) || (old_height != display_h)) {
            bgfx::reset(display_w, display_h);
            old_width = display_w;
            old_height = display_h;
        }

        ui_update(ctx);
        ui_render(ctx, layout, program);

        bgfx::frame();
    }

    bgfx::shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
