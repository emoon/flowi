// SDL window backend, Metal mode (APPLE): SDL-Metal windows carrying a CAMetalLayer. Present composites the UI
// Surface Texture onto the layer's drawable.

#include "sdl_window_internal.h"

#if BACKEND_USE_SDL && FL_WINDOW_HAVE_METAL

#include <core/log.h>

// Host-side Metal compositor. Manual retain/release (no ARC): every long-lived id<> the SdlWindowContext keeps is
// +1 retained on assignment and released on teardown. The incoming Surface Texture is a non-owning cast of the
// void* handle; the render backend owns its retain.

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

static bool sdl_metal_create_window(SdlWindowContext* self, FlWindow* window, const SdlWindowParams* params) {
    // An SDL Metal view carries the CAMetalLayer present pulls drawables from.
    if (!sdl_create_base_window(window, params, SDL_WINDOW_METAL, "metal")) {
        return false;
    }
    window->metal_view = SDL_Metal_CreateView(window->window);
    if (window->metal_view == NULL) {
        log_error("fl_app window: SDL_Metal_CreateView failed: %s", SDL_GetError());
        SDL_DestroyWindow(window->window);
        return false;
    }
    window->metal_layer = (CAMetalLayer*)SDL_Metal_GetLayer(window->metal_view);
    if (window->metal_layer == nil) {
        log_error("fl_app window: SDL_Metal_GetLayer returned no CAMetalLayer");
        SDL_Metal_DestroyView(window->metal_view);
        window->metal_view = NULL;
        SDL_DestroyWindow(window->window);
        return false;
    }
    self->metal_window_count++;
    return true;
}

static void sdl_metal_release_shared_state(SdlWindowContext* self) {
    [self->metal_pipeline release];
    self->metal_pipeline = nil;
    [self->metal_queue release];
    self->metal_queue = nil;
    [self->metal_device release];
    self->metal_device = nil;
    self->metal_ready = false;
}

// The Surface Texture is render-backend-owned; the shared compositor state is released with the last Metal window.
static void sdl_metal_destroy_window(SdlWindowContext* self, FlWindow* window) {
    if (window->metal_view == NULL) {
        return;
    }
    SDL_Metal_DestroyView(window->metal_view);
    window->metal_view = NULL;
    window->metal_layer = nil;
    self->metal_window_count--;
    if (self->metal_window_count <= 0 && self->metal_ready) {
        sdl_metal_release_shared_state(self);
    }
}

// Builds the shared device, queue and pipeline against the texture's MTLDevice. False if one is unavailable.
static bool sdl_metal_build_shared_state(SdlWindowContext* self, id<MTLTexture> ui_tex, MTLPixelFormat color_format) {
    id<MTLDevice> device = [ui_tex device];
    if (device == nil) {
        return false;
    }
    id<MTLCommandQueue> queue = [device newCommandQueue]; // +1 owned
    if (queue == nil) {
        log_error("fl_app window: Metal newCommandQueue failed");
        return false;
    }
    id<MTLRenderPipelineState> pipeline = build_metal_compositor(device, color_format);
    if (pipeline == nil) {
        [queue release];
        return false;
    }
    self->metal_device = [device retain]; // +1 owned (texture holds its own ref; keep ours for teardown)
    self->metal_queue = queue;
    self->metal_pipeline = pipeline;
    self->metal_ready = true;
    return true;
}

// Draws the UI texture into the layer's next drawable and presents it. Skips the frame when no drawable is
// available (e.g. window occluded) rather than block.
static void sdl_metal_composite(SdlWindowContext* self, FlWindow* window, id<MTLTexture> ui_tex) {
    // Keep the layer's drawableSize at the window's pixel size.
    int draw_w = 0, draw_h = 0;
    SDL_Metal_GetDrawableSize(window->window, &draw_w, &draw_h);
    if (draw_w > 0 && draw_h > 0) {
        window->metal_layer.drawableSize = CGSizeMake((CGFloat)draw_w, (CGFloat)draw_h);
    }

    id<CAMetalDrawable> drawable = [window->metal_layer nextDrawable];
    if (drawable == nil) {
        return;
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

// The compositor device/queue/pipeline are built on the first present. Each present uses its own command buffer,
// render pass and drawable.
static void sdl_present_metal(void* ud, FlWindow* window, FlSurfaceHandle surface_texture) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    // A null texture leaves the window untouched.
    id<MTLTexture> ui_tex = (id<MTLTexture>)surface_texture;
    if (window == NULL || window->metal_layer == nil) {
        return;
    }
    if (ui_tex == nil) {
        return;
    }
    if (!self->metal_ready && !sdl_metal_build_shared_state(self, ui_tex, window->metal_layer.pixelFormat)) {
        return;
    }
    // Bind the window's layer to the shared device.
    if (window->metal_layer.device != self->metal_device) {
        window->metal_layer.device = self->metal_device;
    }
    @autoreleasepool {
        sdl_metal_composite(self, window, ui_tex);
    }
}

static void sdl_metal_pixel_size(FlWindow* window, int* px_w, int* px_h) {
    SDL_Metal_GetDrawableSize(window->window, px_w, px_h);
}

const SdlWindowMode k_sdl_window_mode_metal = {
    .mode = FlWindowBackendMode_Metal,
    .name = "Metal",
    .create_window = sdl_metal_create_window,
    .destroy_window = sdl_metal_destroy_window,
    .pixel_size = sdl_metal_pixel_size,
    .present = sdl_present_metal,
};

#endif // BACKEND_USE_SDL && FL_WINDOW_HAVE_METAL
