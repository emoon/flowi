// GPU smoke test: fl_app GPU path and docked viewport3d elements.

#include "gpu_test_common.h"

// fl_app GPU path. The bare-backend cases above drive the render backend directly; this drives the
// fl_app runner on the GPU backend it selects (the headless Surface Binding: EGL-surfaceless on Linux, a bare
// MTLDevice on macOS). The frame draws a rect and a circle (the circle is a GPU-tier IR primitive the
// software backend drops, so its presence in the readback is the GPU path's signature). fl_app owns its own
// headless context, so on Linux this runs after the shared EGL context is torn down.

static void fl_app_gpu_frame(void* user_data) {
    (void)user_data;
    // Paint only records inside an open painting scope (painter.h: a paint call outside any scope is a no-op).
    // A full-size root area is that scope; its origin is (0,0). fl_app composes ui_scale = content_scale x
    // ui_zoom; a headless backend reports content scale 1.0 and the default zoom is 1.0, so the scale is 1.0
    // here and element-local painter coordinates map 1:1 to the 256x256 surface.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        Painter* p = g_ui_ctx->painter;
        painter_rect(p, (FlRect) { .x = 20.0f, .y = 20.0f, .w = 100.0f, .h = 100.0f }, ui_color(40, 60, 200, 255));
        painter_circle(p, (FlVec2) { .x = 180.0f, .y = 180.0f }, 50.0f, ui_color(230, 80, 60, 255));
        // Two green outlines, the rect in the left half and the circle in the right; the readback counts each
        // half's green pixels to tell hollow from filled. A hard green shadow rides along: cast from the
        // ring it changes nothing, cast from the whole shape it would fill the interior.
        FlStroke outline = { .width = 6.0f, .color = ui_color(40, 220, 60, 255) };
        painter_set_effect(p, (FlEffect) { .kind = FlEffectKind_DropShadow, .color = outline.color });
        painter_circle_stroke(p, (FlVec2) { .x = 190.0f, .y = 60.0f }, 40.0f, outline);
        painter_rect_stroke(p, (FlRect) { .x = 20.0f, .y = 150.0f, .w = 80.0f, .h = 80.0f }, 0.0f, outline);
    }
}

// A Viewport3d inside a Detachable Panel, driven through the public API on the fl_app runner: a viewport is a
// regular layout element, so it docks, sizes to its leaf's rect, and reports that rect back for the caller's
// projection matrix.
//
// It takes more than one frame by construction: layout runs after the frame callback, so the first begin
// reports a zero size and has no target to draw yet. Frame 2 onward it renders.

#if !defined(__APPLE__) // only caller is the GL/EGL branch of main(); unused on the Metal path
#define VIEWPORT_PANEL_ID 0x763350u
#define VIEWPORT_PANEL_ID_RIGHT 0x763351u

// The two views of the one shared mesh: the same 0.4 shrink, pushed to opposite ends of its viewport. Both
// panels are full height, so "which band the mesh lands in" reads straight off the framebuffer and does not
// depend on how wide the dock made either panel.
static const f32 k_view_top[16] = {
    0.4f, 0.0f, 0.0f, 0.0f, //
    0.0f, 0.4f, 0.0f, 0.0f, //
    0.0f, 0.0f, 1.0f, 0.0f, //
    0.0f, 0.5f, 0.0f, 1.0f, //
};
static const f32 k_view_bottom[16] = {
    0.4f, 0.0f,  0.0f, 0.0f, //
    0.0f, 0.4f,  0.0f, 0.0f, //
    0.0f, 0.0f,  1.0f, 0.0f, //
    0.0f, -0.5f, 0.0f, 1.0f, //
};

typedef struct Viewport3dFrameState {
    FlMeshHandle mesh; // one mesh, drawn by both viewports; retained meshes are backend-shared
    FlVec2 last_size;
    FlVec2 last_size_right;
    bool viewport_declared;
    bool right_viewport_declared;
    bool layout_authored;
} Viewport3dFrameState;

// Emit one viewport filling panel, drawing the shared mesh through view_projection. Returns the size the
// viewport reported, or a zero size when the panel is not the visible tab this frame.
typedef struct PanelSpec {
    u64 panel;
    FlString name;
    const f32* view_projection;
    bool* declared;
} PanelSpec;

static FlVec2 emit_panel_viewport(Viewport3dFrameState* state, PanelSpec spec) {
    const u64 panel = spec.panel;
    const FlString name = spec.name;
    const f32* view_projection = spec.view_projection;
    bool* declared = spec.declared;
    if (!fl_dock_panel(panel, name)) {
        return (FlVec2) { 0 };
    }
    *declared = true;
    FlVec2 size = fl_viewport3d_begin((FlLayoutId) { .name = name });
    fl_viewport3d_set_view_projection(view_projection, 16);
    fl_viewport3d_draw_mesh(state->mesh, FL_IMAGE_HANDLE_INVALID, FlImageFilter_Linear);
    fl_viewport3d_end();
    fl_dock_panel_end(); // a true fl_dock_panel opened the panel's element inside its tile; this closes it
    return size;
}

static void viewport3d_app_frame(void* user_data) {
    Viewport3dFrameState* state = user_data;

    if (state->mesh == FL_MESH_HANDLE_INVALID) {
        // A single magenta quad filling most of clip space. Created on the first frame, when the UI context
        // (and through it the render backend the seam forwards to) is up.
        TestQuad quad = test_quad(-0.8f, 0.8f, 0.0f, (FlColor) { 220, 40, 200, 255 });
        static const u16 indices[6] = { 0, 1, 2, 0, 2, 3 };
        state->mesh = fl_mesh_create(quad.verts, 4, indices, 6);
    }

    fl_dock_workspace_begin();
    // Author the side-by-side layout once, as a built tree.
    if (!state->layout_authored) {
        state->layout_authored = true;
        dock_workspace_set_tree(dock_workspace_current(),
                                dock_tree_build(dock_split(FlDockAxis_X, 0.5f, dock_leaf(VIEWPORT_PANEL_ID),
                                                           dock_leaf(VIEWPORT_PANEL_ID_RIGHT))));
    }

    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        fl_dock_workspace_emit();
        state->last_size = emit_panel_viewport(
            state, (PanelSpec) { VIEWPORT_PANEL_ID, FL_S("scene"), k_view_top, &state->viewport_declared });
        state->last_size_right
            = emit_panel_viewport(state, (PanelSpec) { VIEWPORT_PANEL_ID_RIGHT, FL_S("scene_right"), k_view_bottom,
                                                       &state->right_viewport_declared });
    }

    fl_dock_workspace_end();
}

typedef struct MeshBand {
    u32 min_y, max_y, count;
} MeshBand;

// Magenta: red and blue high, green low. The dark dock chrome fakes neither.
static bool is_mesh_pixel(const u8* px) {
    return px[3] != 0 && px[0] > 150 && px[2] > 130 && px[1] < 100;
}

static void band_add(MeshBand* band, u32 y) {
    band->count++;
    band->min_y = y < band->min_y ? y : band->min_y;
    band->max_y = y > band->max_y ? y : band->max_y;
}

static void collect_mesh_bands(const FlPixelReadback* rb, MeshBand* left, MeshBand* right) {
    for (u32 y = 0; y < rb->height; ++y) {
        for (u32 x = 0; x < rb->width; ++x) {
            if (!is_mesh_pixel(pixel_at(rb, (int)x, (int)y))) {
                continue;
            }
            band_add(x < rb->width / 2 ? left : right, y);
        }
    }
}

static int check_viewport_state(const Viewport3dFrameState* state) {
    CHECK(state->viewport_declared, "viewport3d app: the panel never became visible, so nothing was exercised");
    CHECK(state->right_viewport_declared, "viewport3d app: the second panel never became visible");
    CHECK(state->mesh != FL_MESH_HANDLE_INVALID, "viewport3d app: mesh_create through the public API failed");
    CHECK(state->last_size.x > 0.0f && state->last_size.y > 0.0f,
          "viewport3d app: begin() must report the element's laid-out pixel size once it has one");
    CHECK(state->last_size_right.x > 0.0f && state->last_size_right.y > 0.0f,
          "viewport3d app: the second viewport must report its own laid-out size");
    return 0;
}

static int check_viewport_bands(const FlPixelReadback* rb) {
    // The two viewports are docked side by side and draw the one shared mesh in the same frame, each through its
    // own matrix: one pushes it to the top of its own rect, the other to the bottom. So split the readback by x,
    // one tile per side, and collect each side's vertical extent: both sides must have drawn, and their two bands
    // must not overlap.
    //
    // The bands are measured against each other rather than against the window's midline: the tiles sit under the
    // workspace's title text and their leaf's tab header, so where the dock puts them is the dock's business.
    MeshBand left = { .min_y = rb->height };
    MeshBand right = { .min_y = rb->height };
    collect_mesh_bands(rb, &left, &right);
    CHECK(left.count > 0, "viewport3d app: the mesh must render inside the docked viewport");
    CHECK(right.count > 0,
          "viewport3d app: the second docked viewport must render the same shared mesh in the same frame");
    // Which of the two ends up higher in the readback is the buffer's orientation, not this test's business.
    enum { MIN_BAND_GAP = 10 };
    CHECK(left.min_y > right.max_y + MIN_BAND_GAP || right.min_y > left.max_y + MIN_BAND_GAP,
          "viewport3d app: each viewport must apply its own view-projection — the two views' meshes must occupy "
          "separate, non-overlapping bands");
    return 0;
}

static int check_viewport_app(FlApp* app, const Viewport3dFrameState* state) {
    if (check_viewport_state(state) != 0) {
        return 1;
    }

    FlPixelReadback rb;
    CHECK(fl_app_read_pixels(app, &rb), "viewport3d app: fl_app_read_pixels failed");
    return check_viewport_bands(&rb);
}

int run_viewport3d_app_test(void) {
    FlAppSettings settings = {
        .window_title = FL_S("viewport3d"),
        .width = 256,
        .height = 256,
        .headless = true,
        .renderer = FlAppRenderer_Gpu,
    };
    FlApp* app = fl_app_create(&settings);
    CHECK(app != nullptr, "viewport3d app: fl_app_create(headless, GPU) failed");

    Viewport3dFrameState state = { .mesh = FL_MESH_HANDLE_INVALID };
    for (int i = 0; i < 4; i++) {
        CHECK(fl_app_tick(app, &state, viewport3d_app_frame), "viewport3d app: tick should keep running");
    }
    if (check_viewport_app(app, &state) != 0) {
        return 1;
    }

    fl_mesh_destroy(state.mesh);
    fl_app_destroy(app);
    printf("gpu_smoke_test: viewport3d docked elements (two viewports, one shared mesh) PASS\n");
    return 0;
}
#endif // !defined(__APPLE__): viewport3d fl_app suite

typedef struct FillScan {
    bool saw_rect;     // blue-dominant
    bool saw_circle;   // red-dominant
    u32 outline_px[2]; // green-dominant, by half: [0] left (rect outline), [1] right (circle outline)
} FillScan;

static bool is_dominant(const u8* px, int channel) {
    int a = px[(channel + 1) % 3];
    int b = px[(channel + 2) % 3];
    return px[channel] > 150 && px[channel] > a && px[channel] > b;
}

// Scan the readback for the two distinct fills, robust to ui_scale and exact coordinates. Fills are stored
// premultiplied, so a fully-covered interior pixel is its own color; the dark theme clear is far below 150
// on every channel, so it fakes neither fill.
static FillScan scan_fills(const FlPixelReadback* rb) {
    FillScan scan = { 0 };
    for (u32 y = 0; y < rb->height; ++y) {
        for (u32 x = 0; x < rb->width; ++x) {
            const u8* px = pixel_at(rb, (int)x, (int)y);
            if (px[3] == 0) {
                continue;
            }
            scan.saw_rect |= is_dominant(px, 2);
            scan.saw_circle |= is_dominant(px, 0);
            scan.outline_px[x >= rb->width / 2u] += is_dominant(px, 1) ? 1u : 0u;
        }
    }
    return scan;
}

static int check_fill_scan(const FillScan* scan) {
    CHECK(scan->saw_rect, "fl_app GPU path: the rect renders (a blue-dominant covered pixel)");
    CHECK(scan->saw_circle, "fl_app GPU path: the circle renders on GPU through fl_app (a red-dominant covered pixel)");
    // A 6px band around an 80px square covers about 1800 px and a 6px ring of radius 40 about 1400; filled,
    // they would cover 6400 and 5000.
    CHECK(scan->outline_px[0] > 1400 && scan->outline_px[0] < 2200,
          "fl_app GPU path: rect_stroke draws the band only, and its shadow is cast from the band");
    CHECK(scan->outline_px[1] > 1000 && scan->outline_px[1] < 1800,
          "fl_app GPU path: circle_stroke draws the ring only, and its shadow is cast from the ring");
    return 0;
}

int run_em_app_gpu_path_test(void) {
    FlAppSettings settings = {
        .window_title = FL_S("fl_app gpu"),
        .width = 256,
        .height = 256,
        .headless = true,
        .renderer = FlAppRenderer_Gpu,
    };
    FlApp* app = fl_app_create(&settings);
    CHECK(app != nullptr, "fl_app_create(headless, GPU) failed");

    CHECK(fl_app_tick(app, nullptr, fl_app_gpu_frame), "fl_app_tick(GPU) should keep running");

    FlPixelReadback rb;
    CHECK(fl_app_read_pixels(app, &rb), "fl_app_read_pixels (GPU) failed");
    CHECK(rb.stride == rb.width * 4u, "GPU readback is RGBA8 (stride == width*4)");

    FillScan scan = scan_fills(&rb);
    if (check_fill_scan(&scan) != 0) {
        return 1;
    }
    printf("gpu_smoke_test: fl_app GPU path (rect + circle + outlines) PASS\n");

    fl_app_destroy(app);
    return 0;
}
