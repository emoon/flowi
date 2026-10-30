///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Viewport3d + Mesh: the core half of flowi's minimal 3D API.
//
// A Viewport3d is an ordinary layout element whose content the render backend rasterizes with the hardware
// pipeline into a color+depth target the element then samples as an ordinary image. The color target is
// realized under a normal FlImageHandle, so clipping, z-order, docking and multi-instance all come from the
// existing image path, and no 3D geometry crosses the render IR.
//
// Per frame a viewport collects a view-projection matrix and a draw list between begin() and end(); end()
// hands them to the backend, which renders into the target immediately - during the frame callback, ahead of
// the UI raster pass that later samples it. The element is emitted at begin() with the handle from the
// previous frame's submit, which is also why begin() reports the previous frame's pixel size (layout runs
// after the callback): a viewport shows nothing on its first frame and is one frame stale across a resize.
//
// A backend with no 3D seam draws a flat placeholder rect instead.

#include "external/clay/clay.h"
#include "image_system.h"
#include "ui_private.h"
#include <core/log.h>
#include <flowi/ui/layout.h>
#include <flowi/ui/viewport3d.h>
#include <string.h>
#include <ui/render_ir.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fixed live-viewport and per-viewport draw caps; overflow is logged.

#define VIEWPORT3D_MAX 8
#define VIEWPORT3D_MAX_DRAWS 256

// What the element fills itself with when there is no color target to draw: a backend with no 3D seam, or a
// viewport's first frame.
#define VIEWPORT3D_PLACEHOLDER_FILL (Clay_Color) { 24, 24, 28, 255 }

// No tint on the color target - the element draws the 3D result as authored.
#define VIEWPORT3D_NO_TINT (Clay_Color) { 255, 255, 255, 255 }

typedef struct Viewport3d {
    u32 element_id;       // the clay id of the viewport element, and the backend's key for its targets
    const FlUiCtx* owner; // the window that last began this viewport
    u64 last_frame;       // owner->frame_counter at that begin
    FlImageHandle color;
    u32 width;
    u32 height;
    f32 view_projection[16];
    FlViewport3dDraw draws[VIEWPORT3D_MAX_DRAWS];
    u32 draw_count;
    bool overflow_diagnosed; // one dropped-draw diagnostic per frame, not one per draw
} Viewport3d;

// Viewports are backend-shared like meshes, and the backend lives on the one per-app UiShared, so
// this table is per-app too. Not per-window state: two windows may each show the same viewport id.
static Viewport3d s_viewports[VIEWPORT3D_MAX];
static u32 s_viewport_count;
static Viewport3d* s_open; // the viewport between begin() and end(), null outside the bracket

static const f32 k_identity[16] = {
    1.0f, 0.0f, 0.0f, 0.0f, //
    0.0f, 1.0f, 0.0f, 0.0f, //
    0.0f, 0.0f, 1.0f, 0.0f, //
    0.0f, 0.0f, 0.0f, 1.0f, //
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The active backend when it implements the 3D seam, else nullptr. The three entries are bound as a group, so
// testing one tests all (see the seam's contract in ui/render_backend.h).
static const FlRenderBackend* renderer_3d(void) {
    FlUiCtx* ctx = ui_get_ctx();
    if (ctx == nullptr) {
        return nullptr;
    }

    const FlRenderBackend* backend = &ctx->shared->renderer;
    return backend->viewport3d_submit != nullptr ? backend : nullptr;
}

// A full table gives the slot of the viewport its owning window began least recently, provided that window has
// run a whole frame without it; a viewport begun this frame or the last is live and never displaced. A slot
// owned by another window cannot be judged from this window's frame counter, so it stays until its owner is
// destroyed (viewport3d_release_owner).
static Viewport3d* find_or_create(u32 element_id) {
    FlUiCtx* ctx = ui_get_ctx();
    const u64 frame = ctx != nullptr ? ctx->frame_counter : 0;
    Viewport3d* stale = nullptr;
    u64 stale_age = 1;
    for (u32 i = 0; i < s_viewport_count; i++) {
        Viewport3d* slot = &s_viewports[i];
        if (slot->element_id == element_id) {
            slot->owner = ctx;
            slot->last_frame = frame;
            return slot;
        }
        const u64 age = frame - slot->last_frame;
        if (slot->owner == ctx && age > stale_age) {
            stale = slot;
            stale_age = age;
        }
    }

    Viewport3d* slot = nullptr;
    if (s_viewport_count < VIEWPORT3D_MAX) {
        slot = &s_viewports[s_viewport_count++];
    } else if (stale != nullptr) {
        slot = stale;
    } else {
        log_error("viewport3d: more than %d live viewports — element %u is not rendered", VIEWPORT3D_MAX,
                  (unsigned)element_id);
        return nullptr;
    }
    *slot = (Viewport3d) { .element_id = element_id, .owner = ctx, .last_frame = frame };
    return slot;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void viewport3d_release_owner(const FlUiCtx* ctx) {
    u32 kept = 0;
    for (u32 i = 0; i < s_viewport_count; i++) {
        if (s_viewports[i].owner != ctx) {
            s_viewports[kept++] = s_viewports[i];
        }
    }
    s_viewport_count = kept;
    if (s_open != nullptr && s_open->owner == ctx) {
        s_open = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Emit the viewport's layout element: a grow-sized box drawing the previous frame's color target as an
// ordinary image, or - before the first submit, and under a backend with no 3D seam - a flat placeholder.
// Tracked by id so the next frame can read back the rect this one lays out.

static void emit_element(FlLayoutId id, FlImageHandle color) {
    fl_layout_start(id);

    Clay_ElementDeclaration decl = {
        .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) } },
    };
    if (color != FL_IMAGE_HANDLE_INVALID) {
        // The same neutral image-source descriptor the image widget stashes; the Clay->IR translator reads it
        // and the backend resolves the handle to its texture on ingest.
        FlImageSource* source = arena_alloc_zero(ui_get_ctx()->render_frame_allocator, FlImageSource);
        source->kind = FlImageKind_Static;
        source->handle = color;
        decl.backgroundColor = VIEWPORT3D_NO_TINT;
        decl.image.imageData = source;
    } else {
        decl.backgroundColor = VIEWPORT3D_PLACEHOLDER_FILL;
    }
    Clay__ConfigureOpenElement(decl);

    fl_layout_end();
    ui_track_element(id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVec2 fl_viewport3d_begin(FlLayoutId id) {
    if (s_open != nullptr) {
        log_error("viewport3d: begin() called inside an open viewport — the previous begin() has no end()");
        return (FlVec2) { 0 };
    }

    u32 element_id = ui_layout_id_to_clay_id(id);
    Viewport3d* self = find_or_create(element_id);
    if (self == nullptr) {
        return (FlVec2) { 0 };
    }

    // The rect this element was laid out at on the previous frame, in device pixels - the size the backend
    // sizes the color+depth target to, and the size the caller builds its projection matrix from.
    FlElementState state = ui_get_element_state_by_id(element_id);
    self->width = (u32)state.size.x;
    self->height = (u32)state.size.y;

    emit_element(id, self->color);

    self->draw_count = 0;
    self->overflow_diagnosed = false;
    memcpy(self->view_projection, k_identity, sizeof(k_identity));
    s_open = self;

    return (FlVec2) { .x = (f32)self->width, .y = (f32)self->height };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_viewport3d_set_view_projection(const float* matrix, uint32_t matrix_size) {
    if (s_open == nullptr) {
        log_error("viewport3d: set_view_projection() outside a begin()/end() bracket");
        return;
    }
    if (matrix == nullptr || matrix_size != 16) {
        log_error("viewport3d: set_view_projection() wants 16 floats, got %u", (unsigned)matrix_size);
        return;
    }
    memcpy(s_open->view_projection, matrix, sizeof(s_open->view_projection));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Append one draw to the open viewport's list - the bracket check, the invalid-mesh drop and the overflow
// diagnostic live here once for both public draw entry points.
static void push_draw(FlViewport3dDraw draw, const char* what) {
    if (s_open == nullptr) {
        log_error("viewport3d: %s() outside a begin()/end() bracket", what);
        return;
    }
    if (draw.mesh == FL_MESH_HANDLE_INVALID) {
        return;
    }
    if (s_open->draw_count >= VIEWPORT3D_MAX_DRAWS) {
        if (!s_open->overflow_diagnosed) {
            log_error("viewport3d: more than %d draws in one frame — the rest are dropped", VIEWPORT3D_MAX_DRAWS);
            s_open->overflow_diagnosed = true;
        }
        return;
    }
    s_open->draws[s_open->draw_count++] = draw;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_viewport3d_draw_mesh(FlMeshHandle mesh, FlImageHandle texture, FlImageFilter filter) {
    push_draw((FlViewport3dDraw) { .mesh = mesh, .texture = texture, .filter = filter, .depth_test = true },
              "draw_mesh");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lines carry no texture and are drawn in their vertex colors; depth testing is the caller's choice.

void fl_viewport3d_draw_lines(FlMeshHandle mesh, bool depth_test) {
    push_draw((FlViewport3dDraw) { .mesh = mesh, .texture = FL_IMAGE_HANDLE_INVALID, .depth_test = depth_test },
              "draw_lines");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_viewport3d_end(void) {
    Viewport3d* self = s_open;
    s_open = nullptr;
    if (self == nullptr) {
        log_error("viewport3d: end() without a matching begin()");
        return;
    }
    const FlRenderBackend* backend = renderer_3d();
    if (backend == nullptr || self->width == 0 || self->height == 0) {
        return; // no 3D seam (the placeholder already drew), or the element has not been laid out yet
    }

    // Mint the color target's image handle once per viewport; the backend registers its own texture under it
    // and acks the readiness registry, after which the element above draws it like any other image.
    if (self->color == FL_IMAGE_HANDLE_INVALID) {
        self->color = image_system_alloc_backend_handle();
    }

    backend->viewport3d_submit(backend->user_data, self->element_id, self->color, self->width, self->height,
                               self->view_projection, self->draws, self->draw_count);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Retained meshes are backend-shared resources with no core-side state: these validate the geometry and forward
// to the backend.

// Triangles and lines differ only in how many indices one primitive takes, so both constructors validate and
// forward through here. per_primitive also names the shape in the diagnostic.
static FlMeshHandle create_mesh(const FlMeshVertex* vertices, u32 vertices_size, const u16* indices, u32 indices_size,
                                FlMeshTopology topology) {
    const u32 per_primitive = topology == FlMeshTopology_Lines ? 2u : 3u;

    if (vertices == nullptr || indices == nullptr || vertices_size == 0 || indices_size == 0) {
        return FL_MESH_HANDLE_INVALID;
    }
    if (indices_size % per_primitive != 0) {
        log_error("mesh_create: %u indices is not a whole number of %s", (unsigned)indices_size,
                  topology == FlMeshTopology_Lines ? "line segments" : "triangles");
        return FL_MESH_HANDLE_INVALID;
    }
    const FlRenderBackend* backend = renderer_3d();
    if (backend == nullptr) {
        return FL_MESH_HANDLE_INVALID;
    }
    return backend->mesh_create(backend->user_data, vertices, vertices_size, indices, indices_size, topology);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlMeshHandle fl_mesh_create(const FlMeshVertex* vertices, uint32_t vertices_size, const uint16_t* indices,
                            uint32_t indices_size) {
    return create_mesh(vertices, vertices_size, indices, indices_size, FlMeshTopology_Triangles);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlMeshHandle fl_mesh_create_lines(const FlMeshVertex* vertices, uint32_t vertices_size, const uint16_t* indices,
                                  uint32_t indices_size) {
    return create_mesh(vertices, vertices_size, indices, indices_size, FlMeshTopology_Lines);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_mesh_destroy(FlMeshHandle mesh) {
    const FlRenderBackend* backend = renderer_3d();
    if (mesh == FL_MESH_HANDLE_INVALID || backend == nullptr) {
        return;
    }
    backend->mesh_destroy(backend->user_data, mesh);
}
