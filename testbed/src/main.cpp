#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

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

        bgfx::frame();
    }

    bgfx::shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
