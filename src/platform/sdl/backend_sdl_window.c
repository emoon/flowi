// SDL window backend: owns the OS connection (SDL video) and creates per-window OS windows in one of three
// construction modes (FlWindowBackendMode). Window lifecycle, the event pump (demuxed per window by SDL windowID
// via the shared sdl_input_common module), cursor mechanics and per-window drawable size + content scale are
// shared by every mode; only presentation differs:
//   GL mode - SDL-GL windows carrying a single shared GL context + the fullscreen-quad compositor; present
//                   composites the GPU render backend's Surface Texture (a plain u32) over the window and swaps.
//                   Requires the GPU build (#if FL_WINDOW_HAVE_GPU gates all GL code).
//   Metal mode - SDL-Metal windows carrying a CAMetalLayer; present composites onto the layer's drawable.
//   Software mode - plain SDL_Renderer windows (no GL context); present_software blits the software render
//                   backend's offscreen RGB24 pixel buffer through a streaming texture.

#include "../window_backend.h"

#if BACKEND_USE_SDL

#include "sdl_input_common.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/log.h>
#include <SDL.h>

#if FL_WINDOW_HAVE_GPU
#include <GL/gl.h>
#include <GL/glext.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Host-side compositor GL entry points (beyond GL 1.x), resolved once through SDL_GL_GetProcAddress.

#define COMPOSITOR_GL_FNS(X)                           \
    X(PFNGLCREATESHADERPROC, CreateShader)             \
    X(PFNGLSHADERSOURCEPROC, ShaderSource)             \
    X(PFNGLCOMPILESHADERPROC, CompileShader)           \
    X(PFNGLGETSHADERIVPROC, GetShaderiv)               \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog)     \
    X(PFNGLDELETESHADERPROC, DeleteShader)             \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram)           \
    X(PFNGLATTACHSHADERPROC, AttachShader)             \
    X(PFNGLLINKPROGRAMPROC, LinkProgram)               \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv)             \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)   \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram)           \
    X(PFNGLUSEPROGRAMPROC, UseProgram)                 \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, Uniform1i)                   \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays)       \
    X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray)       \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture)

typedef struct CompositorGl {
#define COMPOSITOR_GL_MEMBER(type, name) type name;
    COMPOSITOR_GL_FNS(COMPOSITOR_GL_MEMBER)
#undef COMPOSITOR_GL_MEMBER
} CompositorGl;

static bool compositor_gl_load(CompositorGl* gl) {
#define COMPOSITOR_GL_LOAD(type, name)                               \
    gl->name = (type)SDL_GL_GetProcAddress("gl" #name);              \
    if (!gl->name) {                                                 \
        log_error("fl_app window: missing GL entry point gl" #name); \
        return false;                                                \
    }
    COMPOSITOR_GL_FNS(COMPOSITOR_GL_LOAD)
#undef COMPOSITOR_GL_LOAD
    return true;
}

// Fullscreen-quad compositor: the vertex shader emits a screen-covering triangle from gl_VertexID; the fragment
// shader samples the UI texture flipped in V.
static const char* k_vertex_shader = "#version 430 core\n"
                                     "out vec2 v_uv;\n"
                                     "void main() {\n"
                                     "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                                     "    v_uv = p;\n"
                                     "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                                     "}\n";

static const char* k_fragment_shader
    = "#version 430 core\n"
      "in vec2 v_uv;\n"
      "out vec4 frag;\n"
      "uniform sampler2D ui_tex;\n"
      "void main() {\n"
      "    frag = texture(ui_tex, vec2(v_uv.x, 1.0 - v_uv.y));\n" // flip V: texel row 0 is the top UI row
      "}\n";

static GLuint compile_shader(const CompositorGl* gl, GLenum stage, const char* src) {
    GLuint shader = gl->CreateShader(stage);
    gl->ShaderSource(shader, 1, &src, NULL);
    gl->CompileShader(shader);
    GLint ok = 0;
    gl->GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[1024];
        gl->GetShaderInfoLog(shader, sizeof(info), NULL, info);
        log_error("fl_app window: compositor shader compile failed: %s", info);
        gl->DeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint build_compositor_program(const CompositorGl* gl) {
    GLuint vs = compile_shader(gl, GL_VERTEX_SHADER, k_vertex_shader);
    GLuint fs = compile_shader(gl, GL_FRAGMENT_SHADER, k_fragment_shader);
    if (vs == 0 || fs == 0) {
        // Delete the shader that compiled.
        if (vs != 0) {
            gl->DeleteShader(vs);
        }
        if (fs != 0) {
            gl->DeleteShader(fs);
        }
        return 0;
    }
    GLuint prog = gl->CreateProgram();
    gl->AttachShader(prog, vs);
    gl->AttachShader(prog, fs);
    gl->LinkProgram(prog);
    gl->DeleteShader(vs);
    gl->DeleteShader(fs);
    GLint ok = 0;
    gl->GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[1024];
        gl->GetProgramInfoLog(prog, sizeof(info), NULL, info);
        log_error("fl_app window: compositor program link failed: %s", info);
        gl->DeleteProgram(prog);
        return 0;
    }
    return prog;
}

#endif // FL_WINDOW_HAVE_GPU

#if FL_WINDOW_HAVE_METAL
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Host-side Metal compositor (APPLE), the counterpart of the GL compositor above. Manual retain/release (no ARC):
// every long-lived id<> the SdlWindowContext keeps is +1 retained on assignment and released on teardown. The
// incoming Surface Texture is a non-owning cast of the void* handle; the render backend owns its retain.

// Fullscreen-quad MSL compositor: the vertex stage emits a screen-covering triangle from the vertex id; the
// fragment stage samples the UI texture flipped in V and returns it premultiplied.
static const char* k_metal_compositor_src
    = "#include <metal_stdlib>\n"
      "using namespace metal;\n"
      "struct VOut { float4 pos [[position]]; float2 uv; };\n"
      "vertex VOut fl_compositor_vs(uint vid [[vertex_id]]) {\n"
      "    float2 p = float2(float((vid << 1) & 2), float(vid & 2));\n"
      "    VOut o;\n"
      "    o.uv = p;\n"
      "    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);\n"
      "    return o;\n"
      "}\n"
      "fragment float4 fl_compositor_fs(VOut in [[stage_in]], texture2d<float> ui_tex [[texture(0)]]) {\n"
      "    constexpr sampler s(mag_filter::nearest, min_filter::nearest);\n"
      "    return ui_tex.sample(s, float2(in.uv.x, 1.0 - in.uv.y));\n" // flip V: texel row 0 is the top UI row
      "}\n";

// Build the fullscreen-quad compositor pipeline against device, targeting color_format (the layer's drawable
// format). Blends premultiplied src-over. Returns a +1 retained pipeline state, or nil on a failure (logged).
static id<MTLRenderPipelineState> build_metal_compositor(id<MTLDevice> device, MTLPixelFormat color_format) {
    NSError* err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:@(k_metal_compositor_src) options:nil error:&err];
    if (lib == nil) {
        log_error("fl_app window: Metal compositor library compile failed: %s",
                  err ? [[err localizedDescription] UTF8String] : "unknown");
        return nil;
    }
    id<MTLFunction> vs = [lib newFunctionWithName:@"fl_compositor_vs"];
    id<MTLFunction> fs = [lib newFunctionWithName:@"fl_compositor_fs"];
    if (vs == nil || fs == nil) {
        log_error("fl_app window: Metal compositor missing function (vs=%p fs=%p)", (void*)vs, (void*)fs);
        [vs release];
        [fs release];
        [lib release];
        return nil;
    }
    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vs;
    desc.fragmentFunction = fs;
    MTLRenderPipelineColorAttachmentDescriptor* ca = desc.colorAttachments[0];
    ca.pixelFormat = color_format;
    ca.blendingEnabled = YES;
    ca.rgbBlendOperation = MTLBlendOperationAdd;
    ca.alphaBlendOperation = MTLBlendOperationAdd;
    ca.sourceRGBBlendFactor = MTLBlendFactorOne;
    ca.sourceAlphaBlendFactor = MTLBlendFactorOne;
    ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&err]; // +1 owned
    [desc release];
    [vs release];
    [fs release];
    [lib release];
    if (pipeline == nil) {
        log_error("fl_app window: Metal compositor pipeline build failed: %s",
                  err ? [[err localizedDescription] UTF8String] : "unknown");
    }
    return pipeline;
}

#endif // FL_WINDOW_HAVE_METAL

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-window handle - the OS window the host holds opaque and passes back to the per-window seams. window_id
// demuxes the shared OS event queue per window.

typedef struct FlWindow {
    SDL_Window* window;
    u32 window_id;
    SDL_Renderer* renderer;  // software mode only (null in GL/Metal mode); owns the streaming blit
    SDL_Texture* ui_texture; // software mode: streaming RGB24, (re)created to match the source size
    int tex_w;               // current ui_texture size, so a live resize triggers a recreate
    int tex_h;
#if FL_WINDOW_HAVE_METAL
    SDL_MetalView metal_view;  // Metal mode: the SDL Metal view attached to the window; owns the CAMetalLayer
    CAMetalLayer* metal_layer; // Metal mode: the layer present pulls drawables from (owned by the view; not retained)
#endif
} FlWindow;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SDL Window Backend context - the OS connection. In GL mode the single shared GL context + the compositor
// program/VAO also live here, built with the first window and torn down with the last.

#define SDL_WINDOW_PUMP_MAX 32 // upper bound on live windows the per-pump windowID array holds

typedef struct SdlWindowContext {
    struct FlArena* arena;
    SdlInputCommon input;
    FlWindowBackendMode mode; // GL / Metal / software; picks the create_window + present path

#if FL_WINDOW_HAVE_GPU
    SDL_GLContext gl_ctx; // the single shared GL context; null until the first window creates it
    CompositorGl gl;      // compositor GL entry points, resolved once with the shared context
    GLuint program;       // fullscreen-quad compositor program (shared)
    GLuint vao;           // empty VAO the core profile needs to draw (shared)
    bool gl_ready;        // the shared context + compositor are built
    Uint64 last_present;  // perf-counter at the last swap (0 = none) - drives the adaptive frame-time cap
    int window_count;     // live windows; the last one destroyed tears the shared GL state down
#endif
#if FL_WINDOW_HAVE_METAL
    // Shared Metal compositor state, built lazily on the first present against the render backend's device (read
    // from the Surface Texture's .device so the sampled texture and the compositor share one MTLDevice). One
    // queue + one pipeline serve every window's CAMetalLayer. +1 retained; released when the last window is
    // destroyed.
    id<MTLDevice> metal_device;
    id<MTLCommandQueue> metal_queue;
    id<MTLRenderPipelineState> metal_pipeline;
    bool metal_ready;       // device + queue + pipeline built
    int metal_window_count; // live Metal windows; the last one destroyed releases the shared compositor state
#endif
} SdlWindowContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Window lifecycle

// The window every mode opens: same geometry, same high-DPI base flag plus resizable when asked, differing only in
// the mode's own SDL_WINDOW_* flag (plus fullscreen when asked) and the label a failure is logged under. Fills
// window->window and window->window_id.
static bool sdl_create_base_window(FlWindow* window, const char* title, int pos_x, int pos_y, int width, int height,
                                   u32 extra_flags, bool resizable, const char* mode_label) {
    const u32 resizable_flag = resizable ? SDL_WINDOW_RESIZABLE : 0;
    window->window
        = SDL_CreateWindow(title, pos_x, pos_y, width, height, extra_flags | resizable_flag | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window->window == NULL) {
        log_error("fl_app window: SDL_CreateWindow (%s) failed: %s", mode_label, SDL_GetError());
        return false;
    }
    window->window_id = SDL_GetWindowID(window->window);
    return true;
}

static FlWindow* sdl_create_window(void* ud, const FlWindowConfig* config) {
    SdlWindowContext* self = (SdlWindowContext*)ud;

    const int width = config ? config->width : 0;
    const int height = config ? config->height : 0;
    const char* title = (config && config->title.data) ? fl_string_to_cstr(self->arena, config->title) : "flowi";
    // FL_WINDOW_POS_DEFAULT centers the window; any other value is an OS-screen coordinate.
    const int pos_x = (config && config->x != FL_WINDOW_POS_DEFAULT) ? config->x : (int)SDL_WINDOWPOS_CENTERED;
    const int pos_y = (config && config->y != FL_WINDOW_POS_DEFAULT) ? config->y : (int)SDL_WINDOWPOS_CENTERED;
    const u32 fullscreen_flag = (config && config->fullscreen) ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    const bool resizable = config ? config->resizable : true;

    FlWindow* window = arena_alloc_zero(self->arena, FlWindow);

#if FL_WINDOW_HAVE_SOFTWARE
    if (self->mode == FlWindowBackendMode_Software) {
        if (!sdl_create_base_window(window, title, pos_x, pos_y, width, height, fullscreen_flag, resizable,
                                    "software")) {
            return NULL;
        }
        // The streaming texture is created on the first present_software.
        window->renderer = SDL_CreateRenderer(window->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (window->renderer == NULL) {
            log_error("fl_app window: SDL_CreateRenderer failed: %s", SDL_GetError());
            SDL_DestroyWindow(window->window);
            return NULL;
        }
        return window;
    }
#endif // FL_WINDOW_HAVE_SOFTWARE

#if FL_WINDOW_HAVE_METAL
    if (self->mode == FlWindowBackendMode_Metal) {
        // An SDL Metal view carries the CAMetalLayer present pulls drawables from.
        if (!sdl_create_base_window(window, title, pos_x, pos_y, width, height, SDL_WINDOW_METAL | fullscreen_flag,
                                    resizable, "metal")) {
            return NULL;
        }
        window->metal_view = SDL_Metal_CreateView(window->window);
        if (window->metal_view == NULL) {
            log_error("fl_app window: SDL_Metal_CreateView failed: %s", SDL_GetError());
            SDL_DestroyWindow(window->window);
            return NULL;
        }
        window->metal_layer = (CAMetalLayer*)SDL_Metal_GetLayer(window->metal_view);
        if (window->metal_layer == nil) {
            log_error("fl_app window: SDL_Metal_GetLayer returned no CAMetalLayer");
            SDL_Metal_DestroyView(window->metal_view);
            window->metal_view = NULL;
            SDL_DestroyWindow(window->window);
            return NULL;
        }
        self->metal_window_count++;
        return window;
    }
#endif // FL_WINDOW_HAVE_METAL

#if FL_WINDOW_HAVE_GPU
    // EGL-backed GL 4.3 core context. Only the first window creates it; later windows make it current against
    // their own drawable.
    SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "1");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    if (!sdl_create_base_window(window, title, pos_x, pos_y, width, height, SDL_WINDOW_OPENGL | fullscreen_flag,
                                resizable, "gl")) {
        return NULL;
    }

    if (!self->gl_ready) {
        self->gl_ctx = SDL_GL_CreateContext(window->window);
        if (self->gl_ctx == NULL) {
            log_error("fl_app window: SDL_GL_CreateContext failed: %s", SDL_GetError());
            SDL_DestroyWindow(window->window);
            return NULL;
        }
        // Best-effort vsync; sdl_present applies the display-refresh frame cap regardless.
        if (SDL_GL_SetSwapInterval(1) != 0) {
            log_info("fl_app window: vsync rejected (%s); pacing via the display-refresh frame cap", SDL_GetError());
        }
        if (!compositor_gl_load(&self->gl)) {
            SDL_GL_DeleteContext(self->gl_ctx);
            self->gl_ctx = NULL;
            SDL_DestroyWindow(window->window);
            return NULL;
        }
        self->program = build_compositor_program(&self->gl);
        if (self->program == 0) {
            SDL_GL_DeleteContext(self->gl_ctx);
            self->gl_ctx = NULL;
            SDL_DestroyWindow(window->window);
            return NULL;
        }
        self->gl.GenVertexArrays(1, &self->vao); // core profile needs a bound VAO to draw, even with no attributes
        self->gl_ready = true;
    }

    self->window_count++;
    return window;
#else
    return NULL;
#endif // FL_WINDOW_HAVE_GPU
}

static void sdl_destroy_window(void* ud, FlWindow* window) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    (void)self;
    if (window == NULL || window->window == NULL) {
        return;
    }

#if FL_WINDOW_HAVE_SOFTWARE
    if (window->renderer != NULL) {
        if (window->ui_texture != NULL) {
            SDL_DestroyTexture(window->ui_texture);
            window->ui_texture = NULL;
        }
        SDL_DestroyRenderer(window->renderer);
        window->renderer = NULL;
    }
#endif // FL_WINDOW_HAVE_SOFTWARE

#if FL_WINDOW_HAVE_METAL
    // The Surface Texture is render-backend-owned; the shared compositor state is released with the last Metal
    // window.
    if (window->metal_view != NULL) {
        SDL_Metal_DestroyView(window->metal_view);
        window->metal_view = NULL;
        window->metal_layer = nil;
        self->metal_window_count--;
        if (self->metal_window_count <= 0 && self->metal_ready) {
            [self->metal_pipeline release];
            self->metal_pipeline = nil;
            [self->metal_queue release];
            self->metal_queue = nil;
            [self->metal_device release];
            self->metal_device = nil;
            self->metal_ready = false;
        }
    }
#endif // FL_WINDOW_HAVE_METAL

#if FL_WINDOW_HAVE_GPU
    // The last window deletes the shared GL state with the context current against this window's drawable.
    if (self->mode == FlWindowBackendMode_Gl) {
        self->window_count--;
        if (self->window_count <= 0 && self->gl_ready) {
            SDL_GL_MakeCurrent(window->window, self->gl_ctx);
            if (self->vao != 0) {
                self->gl.DeleteVertexArrays(1, &self->vao);
                self->vao = 0;
            }
            if (self->program != 0) {
                self->gl.DeleteProgram(self->program);
                self->program = 0;
            }
            SDL_GL_DeleteContext(self->gl_ctx);
            self->gl_ctx = NULL;
            self->gl_ready = false;
        }
    }
#endif // FL_WINDOW_HAVE_GPU

    SDL_DestroyWindow(window->window);
    window->window = NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Event pump - build the parallel windowID array the shared pump demuxes against (windows[i] -> contexts[i])

static void sdl_pump_events(void* ud, FlWindow** windows, FlUiCtx** contexts, int count) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    if (count > SDL_WINDOW_PUMP_MAX) {
        count = SDL_WINDOW_PUMP_MAX;
    }
    u32 window_ids[SDL_WINDOW_PUMP_MAX];
    for (int i = 0; i < count; i++) {
        window_ids[i] = windows[i] ? windows[i]->window_id : 0;
    }
    sdl_input_common_pump_multi(&self->input, window_ids, contexts, count);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Drawable size + content scale

// Live drawable size in physical pixels, per mode. Shared by drawable_size and content_scale.
static void window_pixel_size(FlWindow* window, int* px_w, int* px_h) {
    if (window->renderer != NULL) {
        SDL_GetRendererOutputSize(window->renderer, px_w, px_h);
#if FL_WINDOW_HAVE_METAL
    } else if (window->metal_layer != nil) {
        SDL_Metal_GetDrawableSize(window->window, px_w, px_h);
#endif
    } else {
        SDL_GL_GetDrawableSize(window->window, px_w, px_h);
    }
}

static void sdl_drawable_size(void* ud, FlWindow* window, int* width, int* height) {
    UNUSED(ud);
    if (window == NULL || window->window == NULL) {
        return;
    }
    int draw_w = 0, draw_h = 0;
    window_pixel_size(window, &draw_w, &draw_h);
    if (width) {
        *width = draw_w;
    }
    if (height) {
        *height = draw_h;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The display's content scale (ddpi / 96) on the Wayland video driver. Returns 0 on any other driver or when the
// DPI is unavailable.
static float sdl_display_index_content_scale(int display_index) {
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver == NULL || SDL_strcmp(driver, "wayland") != 0) {
        return 0.0f;
    }
    float ddpi = 0.0f, hdpi = 0.0f, vdpi = 0.0f;
    if (SDL_GetDisplayDPI(display_index, &ddpi, &hdpi, &vdpi) != 0 || ddpi <= 0.0f) {
        return 0.0f;
    }
    return ddpi / 96.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static float sdl_display_content_scale(SDL_Window* window) {
    return sdl_display_index_content_scale(SDL_GetWindowDisplayIndex(window));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The primary display's scale for sizing a window before it exists; 0 where window sizes are not physical pixels.
static float sdl_primary_display_content_scale(void* ud) {
    UNUSED(ud);
    return sdl_display_index_content_scale(0);
}

static float sdl_content_scale(void* ud, FlWindow* window) {
    UNUSED(ud);
    if (window == NULL || window->window == NULL) {
        return 1.0f;
    }
    // Content scale = backbuffer (pixel) size / logical window size.
    int win_w = 0, win_h = 0, px_w = 0, px_h = 0;
    SDL_GetWindowSize(window->window, &win_w, &win_h);
    window_pixel_size(window, &px_w, &px_h);
    if (win_w <= 0 || px_w <= 0) {
        return 1.0f;
    }
    const float ratio = (float)px_w / (float)win_w;
    if (ratio > 1.0f) {
        return ratio;
    }
    // A ratio of 1 falls back to the display's content scale.
    const float display_scale = sdl_display_content_scale(window->window);
    return display_scale > 0.0f ? display_scale : ratio;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cursor + hidden-drag mechanics - delegated to the shared SDL input state

static void sdl_set_cursor(void* ud, FlWindow* window, FlCursor cursor) {
    UNUSED(window);
    SdlWindowContext* self = (SdlWindowContext*)ud;
    sdl_input_common_set_cursor(&self->input, cursor);
}

static void sdl_set_cursor_visible(void* ud, FlWindow* window, bool visible) {
    UNUSED(ud);
    UNUSED(window);
    sdl_input_common_set_cursor_visible(visible);
}

static void sdl_set_relative_mouse_mode(void* ud, FlWindow* window, bool enabled) {
    UNUSED(window);
    SdlWindowContext* self = (SdlWindowContext*)ud;
    sdl_input_common_set_relative_mouse_mode(&self->input, enabled);
}

static void sdl_warp_mouse(void* ud, FlWindow* window, float x, float y) {
    UNUSED(ud);
    sdl_input_common_warp_mouse(window ? window->window : NULL, x, y);
}

#if FL_WINDOW_HAVE_SOFTWARE

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Presentation - software: blit the CPU pixel buffer to the window through a streaming texture, (re)created when the
// source size changes.

static void sdl_present_software(void* ud, FlWindow* window, const void* px, u32 tex_width, u32 tex_height,
                                 u32 stride) {
    UNUSED(ud);
    if (window == NULL || window->renderer == NULL || px == NULL || tex_width == 0 || tex_height == 0) {
        return;
    }
    if (window->ui_texture == NULL || (u32)window->tex_w != tex_width || (u32)window->tex_h != tex_height) {
        if (window->ui_texture != NULL) {
            SDL_DestroyTexture(window->ui_texture);
            window->ui_texture = NULL;
        }
        window->ui_texture = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                               (int)tex_width, (int)tex_height);
        if (window->ui_texture == NULL) {
            log_error("fl_app window: streaming texture creation failed (%ux%u): %s", tex_width, tex_height,
                      SDL_GetError());
            window->tex_w = 0;
            window->tex_h = 0;
            return;
        }
        SDL_SetTextureScaleMode(window->ui_texture, SDL_ScaleModeNearest); // crisp 1:1 UI->window blit
        window->tex_w = (int)tex_width;
        window->tex_h = (int)tex_height;
    }

    SDL_UpdateTexture(window->ui_texture, NULL, px, (int)stride);
    SDL_SetRenderDrawColor(window->renderer, 0, 0, 0, 255);
    SDL_RenderClear(window->renderer);
    SDL_RenderCopy(window->renderer, window->ui_texture, NULL, NULL);
    SDL_RenderPresent(window->renderer);
}

#endif // FL_WINDOW_HAVE_SOFTWARE

#if FL_WINDOW_HAVE_GPU

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Presentation - GL: composite the UI surface texture over the window with premultiplied src-over blending.

// Bind the shared GL context against a window's drawable without compositing or swapping.
static void sdl_make_current(void* ud, FlWindow* window) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    if (window == NULL || window->window == NULL || !self->gl_ready) {
        return;
    }
    SDL_GL_MakeCurrent(window->window, self->gl_ctx);
}

static void sdl_present(void* ud, FlWindow* window, u32 surface_texture, u32 tex_width, u32 tex_height) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    UNUSED(tex_width);
    UNUSED(tex_height);
    if (window == NULL || window->window == NULL || !self->gl_ready) {
        return;
    }
    // Texture name 0 is a surface that has not rendered yet; leave the window untouched.
    if (surface_texture == 0) {
        return;
    }

    // Bind the shared context against this window's drawable, then composite + swap, setting all GL state per call.
    SDL_GL_MakeCurrent(window->window, self->gl_ctx);

    int draw_w = 0;
    int draw_h = 0;
    SDL_GL_GetDrawableSize(window->window, &draw_w, &draw_h);
    glViewport(0, 0, draw_w, draw_h);
    glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    self->gl.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)surface_texture);
    // Nearest filtering; the surface has a single mip level.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    self->gl.UseProgram(self->program);
    self->gl.Uniform1i(self->gl.GetUniformLocation(self->program, "ui_tex"), 0);
    self->gl.BindVertexArray(self->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Unbind the compositor's state.
    glDisable(GL_BLEND);
    self->gl.BindVertexArray(0);
    self->gl.UseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    SDL_GL_SwapWindow(window->window);

    // Frame cap: sleep out the remainder of the display's refresh period since the last swap (shared across windows).
    int refresh = 0;
    SDL_DisplayMode mode;
    int display_index = SDL_GetWindowDisplayIndex(window->window);
    if (display_index >= 0 && SDL_GetCurrentDisplayMode(display_index, &mode) == 0) {
        refresh = mode.refresh_rate;
    }
    double target = 1.0 / (refresh > 0 ? (double)refresh : 60.0); // 60 Hz when the refresh rate is unknown
    Uint64 now = SDL_GetPerformanceCounter();
    if (self->last_present != 0) {
        double elapsed = (double)(now - self->last_present) / (double)SDL_GetPerformanceFrequency();
        if (elapsed < target) {
            Uint32 sleep_ms = (Uint32)((target - elapsed) * 1000.0);
            if (sleep_ms > 0) {
                SDL_Delay(sleep_ms);
            }
        }
    }
    self->last_present = SDL_GetPerformanceCounter();
}

#endif // FL_WINDOW_HAVE_GPU

#if FL_WINDOW_HAVE_METAL

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Presentation - Metal (APPLE): composite the UI Surface Texture over the window. The compositor device/queue/
// pipeline are built on the first present against the texture's MTLDevice. Each present uses its own command
// buffer, render pass and drawable.

static void sdl_present_metal(void* ud, FlWindow* window, void* surface_texture, u32 tex_width, u32 tex_height) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    UNUSED(tex_width);
    UNUSED(tex_height);
    if (window == NULL || window->metal_layer == nil) {
        return;
    }
    // A null texture leaves the window untouched.
    id<MTLTexture> ui_tex = (id<MTLTexture>)surface_texture;
    if (ui_tex == nil) {
        return;
    }

    if (!self->metal_ready) {
        id<MTLDevice> device = [ui_tex device];
        if (device == nil) {
            return;
        }
        id<MTLCommandQueue> queue = [device newCommandQueue]; // +1 owned
        if (queue == nil) {
            log_error("fl_app window: Metal newCommandQueue failed");
            return;
        }
        id<MTLRenderPipelineState> pipeline = build_metal_compositor(device, window->metal_layer.pixelFormat);
        if (pipeline == nil) {
            [queue release];
            return;
        }
        self->metal_device = [device retain]; // +1 owned (texture holds its own ref; keep ours for teardown)
        self->metal_queue = queue;
        self->metal_pipeline = pipeline;
        self->metal_ready = true;
    }

    // Bind the window's layer to the shared device.
    if (window->metal_layer.device != self->metal_device) {
        window->metal_layer.device = self->metal_device;
    }

    @autoreleasepool {
        // Keep the layer's drawableSize at the window's pixel size.
        int draw_w = 0, draw_h = 0;
        SDL_Metal_GetDrawableSize(window->window, &draw_w, &draw_h);
        if (draw_w > 0 && draw_h > 0) {
            window->metal_layer.drawableSize = CGSizeMake((CGFloat)draw_w, (CGFloat)draw_h);
        }

        id<CAMetalDrawable> drawable = [window->metal_layer nextDrawable];
        if (drawable == nil) {
            return; // no drawable available this frame (e.g. window occluded); skip rather than block
        }

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0.06, 0.06, 0.08, 1.0);

        id<MTLCommandBuffer> cmd = [self->metal_queue commandBuffer];
        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
        [enc setRenderPipelineState:self->metal_pipeline];
        [enc setFragmentTexture:ui_tex atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];
        [cmd presentDrawable:drawable];
        [cmd commit];
    }
}

#endif // FL_WINDOW_HAVE_METAL

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void sdl_window_destroy(void* ud) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    sdl_input_common_destroy(&self->input);
    // SDL subsystems are left up (no SDL_Quit).
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory

FlWindowBackend sdl_window_backend_create(struct FlArena* arena, FlWindowBackendMode mode) {
    // A mode this build lacks returns a zeroed backend.
#if !FL_WINDOW_HAVE_GPU
    if (mode == FlWindowBackendMode_Gl) {
        log_error("SDL window backend: GL mode requires the GPU build");
        return (FlWindowBackend) { 0 };
    }
#endif
#if !FL_WINDOW_HAVE_METAL
    if (mode == FlWindowBackendMode_Metal) {
        log_error("SDL window backend: Metal mode requires the Metal build (APPLE)");
        return (FlWindowBackend) { 0 };
    }
#endif
#if !FL_WINDOW_HAVE_SOFTWARE
    if (mode == FlWindowBackendMode_Software) {
        log_error("SDL window backend: software mode is not built for this platform");
        return (FlWindowBackend) { 0 };
    }
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        log_error("fl_app window: SDL_Init(VIDEO) failed: %s", SDL_GetError());
        return (FlWindowBackend) { 0 };
    }
    if (!(SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER)) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
            log_warning("fl_app window: SDL game controller init failed: %s", SDL_GetError());
        }
    }

    SdlWindowContext* self = arena_alloc_zero(arena, SdlWindowContext);
    self->arena = arena;
    self->mode = mode;
    sdl_input_common_init(&self->input);

    const char* mode_name = mode == FlWindowBackendMode_Software ? "software"
                            : mode == FlWindowBackendMode_Metal  ? "Metal"
                                                                 : "GL";
    log_info("Window Backend: SDL initialized (%s mode)", mode_name);

    FlWindowBackend backend = {
        .user_data = self,
        .create_window = sdl_create_window,
        .destroy_window = sdl_destroy_window,
        .pump_events = sdl_pump_events,
        .drawable_size = sdl_drawable_size,
        .content_scale = sdl_content_scale,
        .display_content_scale = sdl_primary_display_content_scale,
        .set_cursor = sdl_set_cursor,
        .set_cursor_visible = sdl_set_cursor_visible,
        .set_relative_mouse_mode = sdl_set_relative_mouse_mode,
        .warp_mouse = sdl_warp_mouse,
        .destroy = sdl_window_destroy,
    };
    // Exactly one present slot, picked by mode; only GL wires make_current.
#if FL_WINDOW_HAVE_SOFTWARE
    if (mode == FlWindowBackendMode_Software) {
        backend.present_software = sdl_present_software;
    }
#endif
#if FL_WINDOW_HAVE_GPU
    if (mode == FlWindowBackendMode_Gl) {
        backend.present = sdl_present;
        backend.make_current = sdl_make_current;
    }
#endif
#if FL_WINDOW_HAVE_METAL
    if (mode == FlWindowBackendMode_Metal) {
        backend.present = sdl_present_metal;
    }
#endif
    return backend;
}

#else // !BACKEND_USE_SDL

#include <core/core.h>
#include <core/log.h>

FlWindowBackend sdl_window_backend_create(struct FlArena* arena, FlWindowBackendMode mode) {
    UNUSED(arena);
    UNUSED(mode);
    log_error("SDL window backend not available (requires BACKEND_USE_SDL)");
    return (FlWindowBackend) { 0 };
}

#endif // BACKEND_USE_SDL
