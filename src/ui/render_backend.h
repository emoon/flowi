#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <flowi/core/math_data.h>
#include <flowi/ui/font_id.h>
#include <flowi/ui/image.h>
#include <flowi/ui/image_handle.h>
#include <flowi/ui/pixel_readback.h>
#include <flowi/ui/viewport3d.h>
#include "glyph_outline.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlFontCache FlFontCache;

typedef struct FlVectorImage FlVectorImage;

typedef struct FlArena FlArena;

typedef struct FlImageDecodeResult FlImageDecodeResult;

typedef struct FlImageLoadRequest FlImageLoadRequest;

typedef struct FlSurface FlSurface;

// Frame background state: a crossfade between two images plus a clear color.
typedef struct FlBackground {
    // current/incoming image (FL_IMAGE_HANDLE_INVALID = none)
    FlImageHandle current;
    // previous/outgoing image; valid only during a crossfade (else FL_IMAGE_HANDLE_INVALID)
    FlImageHandle prev;
    // 0 = prev only, 1 = current only
    float crossfade_t;
    // current-image opacity when not crossfading (0..255)
    uint8_t alpha;
    // clear color drawn under/instead of the image
    FlColor clear;
} FlBackground;

// Frame envelope.
//
// global_alpha is a frame-wide opacity applied by the backend; emitters never
// pre-multiply it into command colors. commands is an arena of
// FlCmdHeader-prefixed records.
typedef struct FlRenderFrame {
    const void* commands;
    uint64_t bytes;
    // frame-wide opacity, applied by the backend
    uint8_t global_alpha;
    FlBackground background;
} FlRenderFrame;

// Async image-load request id (the by-value cookie prepare_image keys on).
typedef uint64_t FlImageLoadRequestId;
#define FL_IMAGE_LOAD_REQUEST_ID_INVALID 0

// One draw in a Viewport3d's per-frame draw list. The mesh's own topology decides whether it rasterizes as
// triangles or lines; the fields here are what the caller varies per draw.
typedef struct FlViewport3dDraw {
    // the retained geometry to draw
    FlMeshHandle mesh;
    // texture the mesh's UVs sample (ImageHandle::INVALID = vertex colors only)
    FlImageHandle texture;
    // how texture is sampled
    FlImageFilter filter;
    // false draws through whatever is in front, for overlays that must stay visible
    bool depth_test;
} FlViewport3dDraw;

// Render backend interface: one object a host implements to turn the neutral
// render-command IR into pixels. Covers surface lifecycle plus the renderer,
// image, and glyph seams; presentation is excluded.
typedef struct FlRenderBackend {
    // Backend-private state, passed back to every function-field.
    void* user_data;
    // Surface lifecycle. A surface is the per-window render target; the backend
    // owns it and hands back an opaque handle. The host creates one per window at
    // startup and destroys it before the backend. render, change_resolution, and
    // read_pixels are surface-scoped - one backend drives many surfaces - while
    // resource preparation and scale changes stay backend-shared.
    FlSurface* (*create_surface)(void* ud, int32_t width, int32_t height);
    void (*destroy_surface)(void* ud, FlSurface* surface);
    // Renderer seam; presentation is excluded. render and change_resolution act
    // on a surface.
    void (*change_resolution)(void* ud, FlSurface* surface, int32_t width, int32_t height);
    void (*render)(void* ud, FlSurface* surface, const FlRenderFrame* frame);
    void (*destroy)(void* ud);
    // Scale-change seam - triggers atlas regeneration when the UI scale changes.
    // GPU forgets every atlas placement; SW is a no-op.
    void (*scale_changed)(void* ud, float ui_scale);
    // Font-cache seam - the host may swap the shared font layer after create.
    // Nullable: GPU leaves it unbound, SW binds it.
    void (*set_font_cache)(void* ud, FlFontCache* font_cache);
    // Image preparation seam for static images, called on decode-complete. Runs
    // in the job worker (SW) or the begin-frame pump (GL/D3D) per
    // prepare_image_on_job.
    void (*prepare_image)(void* ud, FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded, FlArena* scratch, const FlImageLoadRequest* request);
    void (*release_image)(void* ud, FlImageHandle h);
    // Glyph preparation seam - the core hands the shared outline; the backend
    // caches its own representation. No release_glyph: atlases regenerate wholesale.
    void (*prepare_glyph)(void* ud, FlFontId font, uint16_t size, uint32_t glyph_id, const FlGlyphOutline* outline);
    // Vector preparation seam - parallel to prepare_glyph for SVG vector images.
    // Nullable: only the GPU backend binds it.
    void (*prepare_vector)(void* ud, FlImageHandle handle, uint16_t size, const FlVectorImage* vector);
    // Optional capability - nullable; off the per-frame present path (smoke tests,
    // screenshots). Reads back the given surface's last-rendered pixels.
    bool (*read_pixels)(void* ud, FlSurface* surface, FlPixelReadback* out);
    // 3D seam - the hardware raster path behind Viewport3d and Mesh. Nullable as
    // a group: a backend either binds all three or none, and a core with none
    // draws a placeholder for every viewport.
    //
    // Retained mesh lifecycle. Buffers are backend-shared, never owned by a
    // viewport, so one mesh draws from any of them. topology fixes how the
    // indices group into primitives for the mesh's whole life.
    FlMeshHandle (*mesh_create)(void* ud, const FlMeshVertex* vertices, uint32_t vertex_count, const uint16_t* indices, uint32_t index_count, FlMeshTopology topology);
    void (*mesh_destroy)(void* ud, FlMeshHandle mesh);
    // Render one viewport's draw list into the color+depth target it owns for
    // viewport, (re)creating that target at width x height when the size
    // changed. The color target is realized under color, an ordinary image
    // handle the core minted, so the UI samples the result through the normal
    // image path. Runs during the frame callback, ahead of the UI raster pass.
    // view_projection is 16 column-major floats.
    void (*viewport3d_submit)(void* ud, uint32_t viewport, FlImageHandle color, uint32_t width, uint32_t height, const float* view_projection, const FlViewport3dDraw* draws, uint32_t draw_count);
    // Threading capability for the image-preparation seam. Set (SW): prepare_image
    // runs in the job worker. Unset (default): deferred to the begin-frame pump
    // on the main/render thread (GL/D3D).
    bool prepare_image_on_job;
} FlRenderBackend;


#ifdef __cplusplus
}
#endif
