// GPU smoke test: 3D tier meshes and viewport targets.

#include "gpu_test_common.h"

#if !defined(__APPLE__)

// The 3D tier: retained meshes, the per-viewport color+depth target, and the dedicated-image rule that makes a
// mesh texture addressable by whole-texture UVs. Driven through the backend seams directly (the core's
// Viewport3d element rides on top of exactly these calls), so the debug accessors are reachable.

enum { VP_SURFACE = 128, VP_ID = 4242, VP_W = 64, VP_H = 64, VP_COLOR = 8003 };

static const f32 k_identity_vp[16] = {
    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
};

typedef struct Vp3dCtx {
    FlRenderBackend* backend;
    FlSurface* surface;
    FlArena* ir_arena;
    FlArena* scratch;
    FlRenderFrame frame;
    FlPixelReadback buf;
    FlImageHandle dedicated_handle;
    FlMeshHandle red_mesh;
    FlMeshHandle green_mesh;
    FlMeshHandle blue_mesh;
    FlViewport3dDraw draws[3];
    GpuViewport3dTarget target;
} Vp3dCtx;

typedef struct NameSet {
    u32 seen[8];
    u32 distinct;
} NameSet;

// Record a GL object name if it is not already there.
static void name_set_add(NameSet* set, u32 name) {
    for (u32 i = 0; i < set->distinct; i++) {
        if (set->seen[i] == name) {
            return;
        }
    }
    set->seen[set->distinct++] = name;
}

static bool is_red_pixel(const u8* px) {
    return px[3] != 0 && px[0] > 150 && px[1] < 80 && px[2] < 80;
}

static bool is_green_pixel(const u8* px) {
    return px[3] != 0 && px[1] > 150 && px[0] < 80 && px[2] < 80;
}

static bool is_blue_pixel(const u8* px) {
    return px[3] != 0 && px[2] > 150 && px[0] < 80 && px[1] < 80;
}

static bool is_cyan_pixel(const u8* px) {
    return px[3] != 0 && px[0] < 80 && px[1] > 150 && px[2] > 150;
}

typedef struct PixelRegion {
    u32 x0, x1, y0, y1;
} PixelRegion;

static bool region_has_pixel(const FlPixelReadback* buf, PixelRegion r, bool (*pred)(const u8*)) {
    for (u32 y = r.y0; y < r.y1; y++) {
        for (u32 x = r.x0; x < r.x1; x++) {
            if (pred(pixel_at(buf, (int)x, (int)y))) {
                return true;
            }
        }
    }
    return false;
}

static FlMeshHandle test_mesh_create(const FlRenderBackend* backend, FlMeshDesc desc) {
    return backend->mesh_create(backend->user_data, &desc);
}

static FlMeshHandle create_triangle_mesh(const FlRenderBackend* backend, const TestQuad* quad) {
    static const u16 quad_indices[6] = { 0, 1, 2, 0, 2, 3 };
    return test_mesh_create(backend, (FlMeshDesc) { .vertices = quad->verts,
                                                    .vertex_count = 4,
                                                    .indices = quad_indices,
                                                    .index_count = 6,
                                                    .topology = FlMeshTopology_Triangles });
}

static FlViewport3dSubmit vp_submit(u32 w, u32 h, const FlViewport3dDraw* draws, u32 draw_count) {
    return (FlViewport3dSubmit) { .viewport = VP_ID,
                                  .color = VP_COLOR,
                                  .width = w,
                                  .height = h,
                                  .view_projection = k_identity_vp,
                                  .draws = draws,
                                  .draw_count = draw_count };
}

static void emit_color_target_image(Vp3dCtx* c, u32 w, u32 h) {
    ir_image(c->ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = VP_COLOR }, FlImageMode_Color,
             (FlRect) { 0, 0, (f32)w, (f32)h }, (FlRect) { 0 }, white, FlImageFilter_Nearest, nullptr);
}

// Submit one draw list to a viewport, composite its color target into the surface at 1:1 in the top-left
// corner, and read the surface back.
static bool render_and_read(Vp3dCtx* c, FlViewport3dSubmit submit, FlPixelReadback* out) {
    c->backend->viewport3d_submit(c->backend->user_data, &submit);

    void* start = ir_rewind_begin(c->ir_arena);
    ir_image(c->ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = submit.color }, FlImageMode_Color,
             (FlRect) { 0, 0, (f32)submit.width, (f32)submit.height }, (FlRect) { 0 }, white, FlImageFilter_Nearest,
             nullptr);
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
    return c->backend->read_pixels(c->backend->user_data, c->surface, out);
}

static void submit_only(Vp3dCtx* c, FlViewport3dSubmit submit) {
    c->backend->viewport3d_submit(c->backend->user_data, &submit);
}

static int check_replaced_target(Vp3dCtx* c) {
    // Reopening a viewport id under a new color handle replaces the target: the old handle stops being ready.
    ImageRegistry* registry = image_registry_new(c->scratch);
    FlPixelReadback ignored;
    FlViewport3dSubmit first = { .viewport = 29000, .color = 9050, .width = 8, .height = 8 };
    first.view_projection = k_identity_vp;
    render_and_read(c, first, &ignored);
    CHECK(image_registry_is_ready((ImageHandle)9050, nullptr, nullptr), "viewport3d: the first target is ready");
    FlViewport3dSubmit reopened = first;
    reopened.color = 9051;
    render_and_read(c, reopened, &ignored);
    CHECK(image_registry_is_ready((ImageHandle)9051, nullptr, nullptr), "viewport3d: the reopened target is ready");
    CHECK(!image_registry_is_ready((ImageHandle)9050, nullptr, nullptr),
          "viewport3d: a replaced target must not stay registered as ready");
    image_registry_destroy(registry);
    return 0;
}

static int check_idle_retirement(Vp3dCtx* c) {
    // More distinct viewport ids than the backend's table holds, each shown for two frames and dropped: idle
    // viewports are retired, so every one still builds its target.
    for (u32 n = 0; n < GPU_VIEWPORT3D_CAP + 4; n++) {
        FlViewport3dSubmit submit = { .viewport = 30000 + n, .color = 9100 + n, .width = 8, .height = 8 };
        submit.view_projection = k_identity_vp;
        FlPixelReadback ignored;
        for (int frame = 0; frame < 2; frame++) {
            render_and_read(c, submit, &ignored);
        }
        GpuViewport3dTarget built = gpu_debug_viewport3d_target(c->backend->user_data, 30000 + n);
        CHECK(built.found && built.width == 8,
              "viewport3d: a viewport past the table cap must still build its target once idle ones are retired");
    }
    return 0;
}

static int prepare_lightmaps(Vp3dCtx* c) {
    FlRenderBackend* backend = c->backend;
    // A 2x1 alpha lightmap: opaque left texel, transparent right. Small enough that the atlas would
    // take it, which is what makes it the test for the dedicated flag.
    static u8 lightmap[2] = { 255, 0 };
    FlImageDecodeResult lightmap_decode = { .pixels = lightmap, .width = 2, .height = 1, .stride = 2, .channels = 1 };
    const FlImageHandle atlased_handle = 8002;
    FlImageLoadRequest dedicated_req = { .relative_path = FL_S("lightmap"), .dedicated = true };
    FlImageLoadRequest atlased_req = { .relative_path = FL_S("lightmap"), .dedicated = false };
    CHECK(backend->prepare_image(backend->user_data, c->dedicated_handle, &lightmap_decode, c->scratch, &dedicated_req),
          "prepare_image: the backend reports the image realized");
    CHECK(backend->prepare_image(backend->user_data, atlased_handle, &lightmap_decode, c->scratch, &atlased_req),
          "prepare_image: the backend reports the image realized");

    CHECK(!gpu_debug_image_in_atlas(backend->user_data, c->dedicated_handle),
          "viewport3d: a dedicated image must never enter the image atlas, whatever its size");
    CHECK(gpu_debug_image_in_atlas(backend->user_data, atlased_handle),
          "viewport3d: the same pixels without the flag still atlas — the flag decides, not the size");
    return 0;
}

static int create_depth_meshes(Vp3dCtx* c) {
    // Three quads in clip space. Red (textured) sits alone on the left at z=0. Green and blue overlap exactly
    // on the right, green nearer (z=0.5) than blue (z=0.9), and green is submitted first, so the order is
    // front-to-back. With the depth test off, the later blue draw would overwrite the nearer green one, so
    // saw_green && !saw_blue can only hold if GL_LESS rejected blue's fragments.
    TestQuad red_quad = test_quad(-0.9f, -0.1f, 0.0f, (FlColor) { 255, 0, 0, 255 });
    TestQuad green_quad = test_quad(0.1f, 0.9f, 0.5f, (FlColor) { 0, 255, 0, 255 });
    TestQuad blue_quad = test_quad(0.1f, 0.9f, 0.9f, (FlColor) { 0, 0, 255, 255 });

    c->red_mesh = create_triangle_mesh(c->backend, &red_quad);
    c->green_mesh = create_triangle_mesh(c->backend, &green_quad);
    c->blue_mesh = create_triangle_mesh(c->backend, &blue_quad);
    CHECK(c->red_mesh != FL_MESH_HANDLE_INVALID && c->green_mesh != FL_MESH_HANDLE_INVALID
              && c->blue_mesh != FL_MESH_HANDLE_INVALID,
          "viewport3d: mesh_create must return live handles");
    CHECK(c->red_mesh != c->green_mesh && c->green_mesh != c->blue_mesh, "viewport3d: each mesh gets its own handle");

    // Identity view-projection: the quads' coordinates are already clip space, so the scene is a plain ortho
    // view and every expectation below is a fixed pixel address.
    c->draws[0] = (FlViewport3dDraw) { .mesh = c->green_mesh, .texture = FL_IMAGE_HANDLE_INVALID, .depth_test = true };
    c->draws[1] = (FlViewport3dDraw) { .mesh = c->blue_mesh, .texture = FL_IMAGE_HANDLE_INVALID, .depth_test = true };
    c->draws[2] = (FlViewport3dDraw) {
        .mesh = c->red_mesh, .texture = c->dedicated_handle, .filter = FlImageFilter_Linear, .depth_test = true
    };
    return 0;
}

static int check_depth_target(Vp3dCtx* c) {
    submit_only(c, vp_submit(VP_W, VP_H, c->draws, 3));

    c->target = gpu_debug_viewport3d_target(c->backend->user_data, VP_ID);
    CHECK(c->target.found && c->target.width == VP_W && c->target.height == VP_H,
          "viewport3d: submit must build the target at the requested size");
    CHECK(!gpu_debug_image_in_atlas(c->backend->user_data, VP_COLOR),
          "viewport3d: the color target is a whole texture, never an atlas cell");
    return 0;
}

static bool render_clipped_copies(Vp3dCtx* c) {
    // Draw the viewport's color target back as an ordinary image, clipped to the left half of its 64x64
    // destination. The clip proves the result composites through the normal image path.
    void* start = ir_rewind_begin(c->ir_arena);
    ir_clip_push(c->ir_arena, (FlRect) { 0, 0, 32, 64 });
    emit_color_target_image(c, VP_W, VP_H);
    ir_clip_pop(c->ir_arena);
    ir_image(c->ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = VP_COLOR }, FlImageMode_Color,
             (FlRect) { 64, 0, (f32)VP_W, (f32)VP_H }, (FlRect) { 0 }, white, FlImageFilter_Nearest, nullptr);
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
    return c->backend->read_pixels(c->backend->user_data, c->surface, &c->buf);
}

static int check_depth_readback(Vp3dCtx* c) {
    CHECK(render_clipped_copies(c), "viewport3d: read_pixels failed");

    // The unclipped copy at x=64: quad columns land at 64 + 0.05*64 .. and the y band covers the middle rows.
    // The red quad's left half samples the opaque lightmap texel, its right half the transparent one.
    const PixelRegion copy = { 64, 128, 0, VP_SURFACE };
    CHECK(region_has_pixel(&c->buf, copy, is_red_pixel),
          "viewport3d: the textured quad renders where its lightmap texel is opaque");
    CHECK(region_has_pixel(&c->buf, copy, is_green_pixel), "viewport3d: the nearer untextured quad renders");
    CHECK(!region_has_pixel(&c->buf, copy, is_blue_pixel),
          "viewport3d: the farther quad must be occluded — the depth test did not run");

    // Sampled at the red quad's far-right column, vertically mid-band, where the lightmap texel is
    // transparent, so the UI sees through the viewport.
    CHECK(pixel_transparent(pixel_at(&c->buf, 64 + 28, 32)),
          "viewport3d: a texel the lightmap makes transparent leaves the target transparent");

    // Under the clip, only the left half of the first copy survives; the red quad spans x=3..29 there, so
    // x=20 is inside the clip and x=40 (still inside the quad's destination) is cut away.
    CHECK(!pixel_transparent(pixel_at(&c->buf, 20, 32)), "viewport3d: the clipped copy draws inside the clip rect");
    CHECK(pixel_transparent(pixel_at(&c->buf, 40, 32)), "viewport3d: the clipped copy is cut outside the clip rect");
    return 0;
}

static int check_resize_step(Vp3dCtx* c, u32 w, u32 h, NameSet sets[3]) {
    submit_only(c, vp_submit(w, h, c->draws, 3));
    GpuViewport3dTarget step = gpu_debug_viewport3d_target(c->backend->user_data, VP_ID);
    CHECK(step.found && step.width == w && step.height == h,
          "viewport3d: a resize must rebuild the color+depth target at the new size");
    CHECK(step.viewport_count == c->target.viewport_count,
          "viewport3d: a resize must reuse the viewport entry, not add one");
    CHECK(step.fbo != 0 && step.depth_rb != 0 && step.color_tex != 0,
          "viewport3d: a rebuilt target must have a framebuffer, depth buffer, and color texture");

    name_set_add(&sets[0], step.fbo);
    name_set_add(&sets[1], step.depth_rb);
    name_set_add(&sets[2], step.color_tex);

    // Render between resizes so the slot table drains its dead list (that is where the replaced color
    // textures are freed). Otherwise the deferred frees pile up and the count would be meaningless.
    void* start = ir_rewind_begin(c->ir_arena);
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
    return 0;
}

static int check_resize_recycles_names(Vp3dCtx* c) {
    // Resize: the same viewport id at a new size must rebuild its target in place and free what it replaced.
    // Name liveness (glIsFramebuffer on the old name) cannot test that: GL may hand a deleted name straight
    // back to the next Gen, so leaking and freeing both leave the old name "live". Name recycling is the signal
    // instead: an implementation that frees gets the same handful of names back, while one that leaks must mint
    // a fresh name per resize (dropping the deletes in setup_targets turns this into RESIZE_COUNT names).
    //
    // This depends on the driver, but only one way: leaking cannot recycle, so a failure here is always a real
    // leak, while passing relies on the driver handing freed names back promptly. Mesa (llvmpipe, what CI runs)
    // allocates the lowest free id and does. A driver that never reuses names would fail this spuriously, and
    // then the assertion is what is wrong.
    enum { RESIZE_COUNT = 8, RESIZE_NAME_BUDGET = 3 };
    NameSet sets[3] = { 0 };
    for (u32 i = 0; i < RESIZE_COUNT; i++) {
        // Every iteration changes the size, so none of them short-circuits the rebuild.
        if (check_resize_step(c, 40u + i * 8u, 72u - i * 4u, sets) != 0) {
            return 1;
        }
    }
    CHECK(sets[0].distinct <= RESIZE_NAME_BUDGET,
          "viewport3d: repeated resizes must recycle framebuffer names — a growing set means they leak");
    CHECK(sets[1].distinct <= RESIZE_NAME_BUDGET,
          "viewport3d: repeated resizes must recycle depth-buffer names — a growing set means they leak");
    CHECK(sets[2].distinct <= RESIZE_NAME_BUDGET,
          "viewport3d: repeated resizes must recycle color-texture names — a growing set means they leak");
    return 0;
}

static int check_mesh_destroy(Vp3dCtx* c) {
    // Destroy the green quad: the blue one behind it becoming visible is only possible if the draw was dropped
    // rather than left pointing at dead buffers.
    c->backend->mesh_destroy(c->backend->user_data, c->green_mesh);
    CHECK(render_and_read(c, vp_submit(VP_W, VP_H, c->draws, 3), &c->buf),
          "viewport3d: read_pixels failed after destroy");

    const PixelRegion target = { 0, 64, 0, 64 };
    CHECK(!region_has_pixel(&c->buf, target, is_green_pixel), "viewport3d: a destroyed mesh stops drawing");
    CHECK(region_has_pixel(&c->buf, target, is_blue_pixel),
          "viewport3d: the quad the destroyed one used to hide becomes visible");
    return 0;
}

static int check_line_draws(Vp3dCtx* c, FlMeshHandle* occluder_mesh, FlMeshHandle* line_mesh) {
    // Line draws, with and without the depth test. A white quad sits in front (z=0.2) of a cyan line (z=0.8)
    // that crosses it horizontally, and the line is submitted second: depth-tested the quad hides it, with the
    // test off the same line comes through.
    const FlColor cyan = { 0, 255, 255, 255 };
    TestQuad occluder = test_quad(-0.9f, 0.9f, 0.2f, (FlColor) { 255, 255, 255, 255 });
    const FlMeshVertex line_verts[2] = {
        { .x = -0.9f, .y = 0.0f, .z = 0.8f, .color = cyan },
        { .x = 0.9f, .y = 0.0f, .z = 0.8f, .color = cyan },
    };
    static const u16 line_indices[2] = { 0, 1 };

    *occluder_mesh = create_triangle_mesh(c->backend, &occluder);
    *line_mesh = test_mesh_create(c->backend, (FlMeshDesc) { .vertices = line_verts,
                                                             .vertex_count = 2,
                                                             .indices = line_indices,
                                                             .index_count = 2,
                                                             .topology = FlMeshTopology_Lines });
    CHECK(*line_mesh != FL_MESH_HANDLE_INVALID, "viewport3d: a line mesh must create like any other");

    FlViewport3dDraw line_draws[2] = {
        { .mesh = *occluder_mesh, .texture = FL_IMAGE_HANDLE_INVALID, .depth_test = true },
        { .mesh = *line_mesh, .texture = FL_IMAGE_HANDLE_INVALID, .depth_test = false },
    };
    const PixelRegion target = { 0, VP_W, 0, VP_H };
    CHECK(render_and_read(c, vp_submit(VP_W, VP_H, line_draws, 2), &c->buf),
          "viewport3d: read_pixels failed for the line drawn without the depth test");
    CHECK(region_has_pixel(&c->buf, target, is_cyan_pixel),
          "viewport3d: a line with the depth test off draws through the geometry in front of it");

    line_draws[1].depth_test = true;
    CHECK(render_and_read(c, vp_submit(VP_W, VP_H, line_draws, 2), &c->buf),
          "viewport3d: read_pixels failed for the depth-tested line");
    CHECK(!region_has_pixel(&c->buf, target, is_cyan_pixel),
          "viewport3d: the same line depth-tested is occluded by the quad in front of it");
    return 0;
}

static int check_filtering(Vp3dCtx* c, FlMeshHandle* full_mesh) {
    // Per-draw sampler filtering. One quad covering the whole viewport, textured with the 2x1 alpha lightmap,
    // drawn once per filter. Nearest reproduces the two texels as they are (every pixel fully opaque or fully
    // transparent) while linear leaves partial alpha across the texel boundary, so the count of
    // partially-transparent pixels along the middle row separates the two.
    TestQuad full_quad = test_quad(-1.0f, 1.0f, 0.0f, (FlColor) { 255, 255, 255, 255 });
    *full_mesh = create_triangle_mesh(c->backend, &full_quad);

    u32 partial_alpha[2] = { 0, 0 }; // indexed by FlImageFilter
    const FlImageFilter filters[2] = { FlImageFilter_Linear, FlImageFilter_Nearest };
    for (u32 f = 0; f < 2; f++) {
        const FlViewport3dDraw textured
            = { .mesh = *full_mesh, .texture = c->dedicated_handle, .filter = filters[f], .depth_test = true };
        CHECK(render_and_read(c, vp_submit(VP_W, VP_H, &textured, 1), &c->buf),
              "viewport3d: read_pixels failed for the filtered draw");
        for (u32 x = 0; x < VP_W; x++) {
            const u8* px = pixel_at(&c->buf, (int)x, (int)(VP_H / 2));
            partial_alpha[filters[f]] += (px[3] != 0 && px[3] != 255) ? 1u : 0u;
        }
    }
    CHECK(partial_alpha[FlImageFilter_Nearest] == 0,
          "viewport3d: nearest filtering shows the lightmap's texels as authored, with no blend between them");
    CHECK(partial_alpha[FlImageFilter_Linear] > 0,
          "viewport3d: linear filtering does blend across the texel boundary — otherwise the case above proves "
          "nothing about the filter");
    return 0;
}

static int run_line_and_filter_checks(Vp3dCtx* c) {
    FlMeshHandle occluder_mesh;
    FlMeshHandle line_mesh;
    FlMeshHandle full_mesh;
    if (check_line_draws(c, &occluder_mesh, &line_mesh) != 0) {
        return 1;
    }
    if (check_filtering(c, &full_mesh) != 0) {
        return 1;
    }
    FlRenderBackend* backend = c->backend;
    backend->mesh_destroy(backend->user_data, occluder_mesh);
    backend->mesh_destroy(backend->user_data, line_mesh);
    backend->mesh_destroy(backend->user_data, full_mesh);
    return 0;
}

static int run_depth_scene_checks(Vp3dCtx* c) {
    if (prepare_lightmaps(c) != 0) {
        return 1;
    }
    if (create_depth_meshes(c) != 0) {
        return 1;
    }
    if (check_depth_target(c) != 0) {
        return 1;
    }
    return check_depth_readback(c);
}

static int run_scene_checks(Vp3dCtx* c) {
    if (check_replaced_target(c) != 0) {
        return 1;
    }
    if (check_idle_retirement(c) != 0) {
        return 1;
    }
    return run_depth_scene_checks(c);
}

static int run_lifecycle_checks(Vp3dCtx* c) {
    if (check_resize_recycles_names(c) != 0) {
        return 1;
    }
    if (check_mesh_destroy(c) != 0) {
        return 1;
    }
    return run_line_and_filter_checks(c);
}

int run_viewport3d_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    CHECK(backend->mesh_create != nullptr && backend->mesh_destroy != nullptr && backend->viewport3d_submit != nullptr,
          "viewport3d: the GL backend must bind the whole 3D seam");

    Vp3dCtx ctx = {
        .backend = backend,
        .surface = backend->create_surface(backend->user_data, VP_SURFACE, VP_SURFACE),
        .ir_arena = ir_arena,
        .scratch = arena_new(),
        .frame = test_frame(),
        .dedicated_handle = 8001,
    };

    if (run_scene_checks(&ctx) != 0) {
        return 1;
    }
    if (run_lifecycle_checks(&ctx) != 0) {
        return 1;
    }

    backend->mesh_destroy(backend->user_data, ctx.red_mesh);
    backend->mesh_destroy(backend->user_data, ctx.blue_mesh);
    fl_arena_destroy(ctx.scratch);
    backend->destroy_surface(backend->user_data, ctx.surface);
    printf("gpu_smoke_test: viewport3d (depth, lines, filtering, dedicated texture, clip, resize) PASS\n");
    return 0;
}

#endif // !defined(__APPLE__)
