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

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void error_callback(int error, const char* description) {
    fprintf(stderr, "Error: %s\n", description);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* glfwNativeWindowHandle(GLFWwindow* _window) {
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
    return (void*)(uintptr_t)glfwGetX11Window(_window);
#elif BX_PLATFORM_OSX
    return glfwGetCocoaWindow(_window);
#elif BX_PLATFORM_WINDOWS
    return glfwGetWin32Window(_window);
#else
    #error unsupported platform
    return nullptr;
#endif
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

    bgfx::Init bgfxInit;
    //bgfxInit.type = bgfx::RendererType::Count;
    bgfxInit.type = bgfx::RendererType::OpenGL;
    bgfxInit.resolution.width = WINDOW_WIDTH;
    bgfxInit.resolution.height = WINDOW_HEIGHT;
    bgfxInit.resolution.reset = 0;//BGFX_RESET_VSYNC;
    bgfxInit.platformData = pd;

    if (!bgfx::init(bgfxInit)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00ff00ff, 1.0f, 0);
    bgfx::setDebug(BGFX_DEBUG_STATS);

    while (!glfwWindowShouldClose(window)) {
        bgfx::setViewRect(0, 0, 0, uint16_t(WINDOW_WIDTH), uint16_t(WINDOW_HEIGHT));
        bgfx::touch(0);
        bgfx::frame();

        glfwPollEvents();
    }

    bgfx::shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
