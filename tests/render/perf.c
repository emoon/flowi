// perf fence scene tests

#include "render/common.h"

// perf_fence is the performance regression check. scene_raster_fill isolates the SW raster fill, border and
// rounded-rect paths and asserts on deterministic work counters (render_commands, bin_alloc_bytes,
// clip_broadcasts). Wall-clock ns is printed and never asserted.

// Merges the core's command count with the SW backend's bin and clip counters. Call after a rendered frame.
static FlFrameStats perf_fence_frame_stats(FlUiCtx* ctx) {
    FlFrameStats stats = { 0 };
    stats.render_commands = (u32)ui_get_render_command_count();
    sw_renderer_read_frame_stats(ctx->shared->renderer.user_data, &stats);
    return stats;
}

// One headless frame: a full-screen container holding N fixed-size children cycling through a solid fill, a
// bordered rect, and a rounded rect.
static void scene_raster_fill(FlUiCtx* ctx, int n) {
    const FlColor fill = ui_color(80, 120, 200, 255);
    const FlColor border = ui_color(220, 200, 60, 255);

    ui_update_begin(ctx);
    fl_layout_start(ID("scene_raster_fill_root"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { .width = sizing_grow(), .height = sizing_grow() },
        .direction = FlLayoutDirection_TopToBottom,
        .color = ui_color(20, 20, 24, 255),
        // The vertical clip goes through the SW clip-broadcast path (a scissor pair sent to every tile), so
        // clip_broadcasts is non-zero.
        .clip = { .vertical = true },
    });
    for (int i = 0; i < n; i++) {
        fl_layout_start(ID_INT("scene_raster_fill_child", i));
        FlLayoutConfig cfg = {
            .sizing = { sizing_fixed(200), sizing_fixed(120) },
            .color = fill,
        };
        switch (i % 3) {
            case 1:
                cfg.border
                    = (FlLayoutBorder) { .color = border, .width = { .left = 2, .right = 2, .top = 2, .bottom = 2 } };
                break;
            case 2:
                cfg.radius = (FlCornerRadius) { .tl = 12, .tr = 12, .bl = 12, .br = 12 };
                break;
            default:
                break;
        }
        fl_layout_configure(cfg);
        fl_layout_end();
    }
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);
}

typedef void (*PerfScene)(FlUiCtx* ctx, int n);

// Runs one scene frame, prints its wall-clock time and counters for a human to read (never asserted), and returns
// the counters.
static FlFrameStats run_perf_scene(FlUiCtx* ctx, const char* name, PerfScene scene, int n) {
    const u64 t0 = get_monotonic_time_ns_os();
    scene(ctx, n);
    const u64 t1 = get_monotonic_time_ns_os();

    const FlFrameStats stats = perf_fence_frame_stats(ctx);
    printf("[perf_fence] %s(N=%d): %llu ns, render_commands=%u bin_alloc_bytes=%u clip_broadcasts=%u\n", name, n,
           (unsigned long long)(t1 - t0), stats.render_commands, stats.bin_alloc_bytes, stats.clip_broadcasts);
    return stats;
}

UTEST(perf_fence, scene_raster_fill) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });

    const FlFrameStats stats = run_perf_scene(ctx, "scene_raster_fill", scene_raster_fill, 128);

    // Deterministic work counters.
    ASSERT_EQ_MSG(stats.render_commands, 16u, "render_commands baseline drift");
    ASSERT_EQ_MSG(stats.bin_alloc_bytes, 87720u, "bin_alloc_bytes baseline drift");
    ASSERT_EQ_MSG(stats.clip_broadcasts, 1020u, "clip_broadcasts baseline drift");
}

// scene_painter_rects measures the submission cost of a dense scope-style canvas (SCOPE_CHANNELS waveforms with
// one rectangle per horizontal logical pixel) through the public per-rect call and through the batched one. Both
// frames leave the same work, meaning the same command count and raster bins, which is asserted. The time spent
// inside the painter calls is printed and never asserted.

#define SCOPE_CHANNELS 4

#define SCOPE_WIDTH 1920

static FlColoredRect g_scope_rects[SCOPE_CHANNELS][SCOPE_WIDTH];

static void scope_rects_init(void) {
    for (int c = 0; c < SCOPE_CHANNELS; c++) {
        const float mid = 120.0f + (float)c * 240.0f;
        for (int x = 0; x < SCOPE_WIDTH; x++) {
            const float amp = 80.0f * sinf((float)x * 0.05f + (float)c);
            g_scope_rects[c][x] = (FlColoredRect) { (float)x, mid - (amp > 0.0f ? amp : 0.0f), 1.0f, fabsf(amp) + 1.0f,
                                                    ui_color((u8)(60 + c * 50), (u8)(220 - c * 40), 120, 180) };
        }
    }
}

// Returns the nanoseconds spent inside the painter calls of one frame.
static void submit_scope_channel(int channel, bool batch) {
    if (batch) {
        fl_painter_rects(g_scope_rects[channel], SCOPE_WIDTH);
        return;
    }
    for (int x = 0; x < SCOPE_WIDTH; x++) {
        const FlColoredRect* r = &g_scope_rects[channel][x];
        fl_painter_rect(r->x, r->y, r->w, r->h, r->color);
    }
}

static u64 scene_painter_rects(FlUiCtx* ctx, bool batch) {
    ui_update_begin(ctx);
    fl_layout_start(ID("scene_painter_rects_root"));
    fl_layout_configure((FlLayoutConfig) { .sizing = { .width = sizing_grow(), .height = sizing_grow() } });

    const u64 t0 = get_monotonic_time_ns_os();
    for (int c = 0; c < SCOPE_CHANNELS; c++) {
        submit_scope_channel(c, batch);
    }
    const u64 t1 = get_monotonic_time_ns_os();

    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);
    return t1 - t0;
}

UTEST(perf_fence, scene_painter_rects) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ctx->layout_width = (float)SCOPE_WIDTH;
    ctx->layout_height = 1080.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)SCOPE_WIDTH, .height = 1080.0f });
    scope_rects_init();

    FlFrameStats stats[2];
    u64 best_ns[2] = { UINT64_MAX, UINT64_MAX };
    for (int mode = 0; mode < 2; mode++) {
        scene_painter_rects(ctx, mode != 0); // warm-up frame
        for (int rep = 0; rep < 9; rep++) {
            const u64 ns = scene_painter_rects(ctx, mode != 0);
            best_ns[mode] = ns < best_ns[mode] ? ns : best_ns[mode];
        }
        stats[mode] = perf_fence_frame_stats(ctx);
    }

    printf("[perf_fence] scene_painter_rects(%d rects): scalar loop %llu ns, batched %llu ns in painter calls "
           "(best of 9 frames), render_commands=%u bin_alloc_bytes=%u\n",
           SCOPE_CHANNELS * SCOPE_WIDTH, (unsigned long long)best_ns[0], (unsigned long long)best_ns[1],
           stats[1].render_commands, stats[1].bin_alloc_bytes);

    // The batch gives the backend exactly the work the scalar loop does.
    ASSERT_TRUE_MSG(stats[1].render_commands > 0, "the scope canvas rendered");
    ASSERT_EQ_MSG(stats[1].render_commands, stats[0].render_commands, "batched render_commands == scalar loop's");
    ASSERT_EQ_MSG(stats[1].bin_alloc_bytes, stats[0].bin_alloc_bytes, "batched raster bin bytes == scalar loop's");
}

// perf_fence CPU micro-scenes, in addition to scene_raster_fill. Each isolates one hot path, so a regression
// points at a cause, and they use the same harness as above.

// scene_text_elided: N narrow boxes, each holding a long single-word label that has to elide (truncate to the
// longest prefix that fits, plus U+2026). It loads the uncached O(chars^2) elide measurement, since every label
// is measured again each frame.
static void scene_text_elided(FlUiCtx* ctx, int n) {
    render_use_default_font(ctx, 2); // built-in 8x8 font at scale 2
    const FlString label = { .data = "AAAAAAAAAAAAAAAAAAAA", .length = 20, .is_static = 1 }; // 20 glyphs; box fits 4
    const FlColor white = { 240, 240, 240, 255 };

    ui_update_begin(ctx);
    fl_layout_start(ID("scene_text_elided_root"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { .width = sizing_grow(), .height = sizing_grow() },
        .direction = FlLayoutDirection_TopToBottom,
        .color = ui_color(20, 20, 24, 255),
        .clip = { .vertical = true },
    });
    for (int i = 0; i < n; i++) {
        fl_layout_start(ID_INT("scene_text_elided_box", i));
        fl_layout_configure((FlLayoutConfig) { .sizing = { sizing_fixed(64), sizing_fixed(20) } });
        ui_text_styled(label, .color = white, .no_wrap = true, .overflow = FlOverflow_Elide);
        fl_layout_end();
    }
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(perf_fence, scene_text_elided) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 1920.0f, 1080.0f);
    const FlFrameStats stats = run_perf_scene(ctx, "scene_text_elided", scene_text_elided, 256);

    ASSERT_EQ_MSG(stats.render_commands, 58u, "render_commands baseline drift");
    ASSERT_EQ_MSG(stats.bin_alloc_bytes, 454920u, "bin_alloc_bytes baseline drift");
}

UTEST(perf_fence, scene_many_elements) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // 16384 is above Clay's default cap of 8192, so it depends on the fixture's Clay_SetMaxElementCount.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    const FlFrameStats stats = run_perf_scene(ctx, "scene_many_elements", scene_many_elements, 16384);

    ASSERT_EQ_MSG(stats.render_commands, 274u, "render_commands baseline drift");
    ASSERT_EQ_MSG(stats.bin_alloc_bytes, 558960u, "bin_alloc_bytes baseline drift");
}

// scene_floating: N floating overlays attached to the root, each with its own z_index and a small colored fill.
// It loads the bubble sort of floating roots by z-index, which is O(N^2).
static void scene_floating(FlUiCtx* ctx, int n) {
    const FlColor fill = ui_color(200, 120, 60, 255);

    ui_update_begin(ctx);
    fl_layout_start(ID("scene_floating_root"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { .width = sizing_grow(), .height = sizing_grow() },
        .color = ui_color(20, 20, 24, 255),
    });
    for (int i = 0; i < n; i++) {
        fl_floating_area_id(FL_ID_INT("scene_floating_overlay", i), .attach_to = FlFloatingAttachTo_Root,
                            .z_index = (int16_t)i, .sizing = { sizing_fixed(16), sizing_fixed(16) }) {
            fl_layout_start(ID_INT("scene_floating_fill", i));
            fl_layout_configure((FlLayoutConfig) { .sizing = { sizing_grow(), sizing_grow() }, .color = fill });
            fl_layout_end();
        }
    }
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(perf_fence, scene_floating) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // z_index 0..255, so 256^2 bubble-sort comparisons
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    const FlFrameStats stats = run_perf_scene(ctx, "scene_floating", scene_floating, 256);

    ASSERT_EQ_MSG(stats.render_commands, 257u, "render_commands baseline drift");
    ASSERT_EQ_MSG(stats.bin_alloc_bytes, 524280u, "bin_alloc_bytes baseline drift");
}

// scene_dyn_images: N draws of one small source image at a scaled destination size, so each draw is a
// RenderMask_TextureScaled command that rebuilds its shuffle table every frame (calculate_shuffle_tables). The
// scene isolates that rebuild. stat.scaled_image_setups equals N, and a caching optimization would lower it.
static ImageHandle s_dyn_image
    = IMAGE_HANDLE_INVALID; // set by the UTEST before the run and read by the scene, whose signature is fixed

static void scene_dyn_images(FlUiCtx* ctx, int n) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        Painter* p = ctx->painter;
        for (int i = 0; i < n; i++) {
            const float x = (float)((i % 60) * 20); // a grid 60 wide of 16px draws, all on screen within 1920x1080
            const float y = (float)((i / 60) * 20);
            painter_image(p, (FlRect) { .x = x, .y = y, .w = 16.0f, .h = 16.0f }, (FlImageHandle)s_dyn_image,
                          (FlColor) { 255, 255, 255, 255 });
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(perf_fence, scene_dyn_images) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 1920.0f, 1080.0f);
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(20, 20, 24);

    // k_png_red is a 4x4 RGB PNG. Drawing it at 16x16, where dst differs from the source width, takes the
    // scaled-texture path.
    s_dyn_image = image_load_from_memory(k_png_red, sizeof(k_png_red), (FlImageLoadOptions) { 0 });
    ASSERT_TRUE_MSG(s_dyn_image != IMAGE_HANDLE_INVALID, "image load returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(s_dyn_image), "image did not decode in time");

    const FlFrameStats stats = run_perf_scene(ctx, "scene_dyn_images", scene_dyn_images, 256);

    ASSERT_EQ_MSG(stats.scaled_image_setups, 256u,
                  "scaled_image_setups baseline drift"); // one shuffle rebuild per image
    ASSERT_EQ_MSG(
        stats.render_commands, 3u,
        "render_commands baseline drift"); // painter draws become one CUSTOM span and not one Clay command per image
}

// scene_widget_gallery: a representative full frame adapted from examples/widget_gallery. No input is pumped, so
// no interaction happens, and the fixed widget state makes every run identical. The two dialogs' open bools are
// false, so ui_message_box returns early. n is the row count of the virtualized List.
#define GALLERY_DECL_ROWS 12

#define GALLERY_SECTION                                                                                   \
    ui_layout_area(.direction = LayoutDirection_TopToBottom, .child_gap = 10, .padding = padding_all(16), \
                   .sizing = { .width = sizing_grow() }, .color = ui_color(32, 34, 42, 255),              \
                   .radius = corner_radius(8))

static void gallery_section_header(FlString title) {
    ui_text_styled(title, .color = ui_color(150, 200, 255, 255));
}

#define GALLERY_MAX_LIST_ROWS 512

typedef struct GalleryState {
    // Clay keeps the text char pointers until ui_update_end without copying, so a dynamic label has to outlive the
    // frame.
    char clicked_buf[32];
    int clicks;
    bool checkbox, toggle;
    float volume;
    int count, dropdown_choice, radio_choice, tab;
    bool dropdown_open, alert_open, confirm_open;
    int list_rows;
    FlSelectionSet* list_sel;
    FlSelectionSet* decl_sel;
} GalleryState;

static u64 s_gallery_list_ids[GALLERY_MAX_LIST_ROWS];
static FlString s_gallery_list_labels[GALLERY_MAX_LIST_ROWS];
static char s_gallery_list_label_buf[GALLERY_MAX_LIST_ROWS][16];
static FlListItem s_gallery_decl_items[GALLERY_DECL_ROWS];
static char s_gallery_decl_label_buf[GALLERY_DECL_ROWS][16];

static void gallery_prepare_lists(GalleryState* g, int n) {
    g->list_rows = min(n, GALLERY_MAX_LIST_ROWS);
    for (int i = 0; i < g->list_rows; i++) {
        s_gallery_list_ids[i] = (u64)(i + 1);
        const int len = snprintf(s_gallery_list_label_buf[i], sizeof(s_gallery_list_label_buf[i]), "Item %d", i + 1);
        s_gallery_list_labels[i] = (FlString) { .data = s_gallery_list_label_buf[i], .length = (u32)len };
    }
    for (int i = 0; i < GALLERY_DECL_ROWS; i++) {
        const int len = snprintf(s_gallery_decl_label_buf[i], sizeof(s_gallery_decl_label_buf[i]), "Row %d", i + 1);
        s_gallery_decl_items[i]
            = (FlListItem) { .id = (u64)(i + 1),
                             .label = (FlString) { .data = s_gallery_decl_label_buf[i], .length = (u32)len },
                             .icon = FL_IMAGE_HANDLE_INVALID };
    }
    g->list_sel = selection_set_create(64);
    g->decl_sel = selection_set_create(16);
}

static void gallery_menubar(GalleryState* g) {
    ui_menubar(ID("gallery_menubar")) {
        ui_menubar_menu(FL_S("File")) {
            ui_menu_item(FL_S("New"), .shortcut = FL_S("Ctrl+N"));
            ui_menu_item(FL_S("Open"), .shortcut = FL_S("Ctrl+O"));
            ui_menu_separator();
            ui_menu_item(FL_S("Save"), .shortcut = FL_S("Ctrl+S"), .disabled = true);
            ui_submenu(FL_S("Recent")) {
                ui_menu_item(FL_S("project.em"));
                ui_submenu(FL_S("Older")) {
                    ui_menu_item(FL_S("draft.em"));
                    ui_menu_item(FL_S("scratch.em"));
                }
            }
        }
        ui_menubar_menu(FL_S("Edit")) {
            ui_menu_item(FL_S("Undo"), .shortcut = FL_S("Ctrl+Z"));
            ui_menu_item(FL_S("Redo"), .shortcut = FL_S("Ctrl+Shift+Z"));
            ui_menu_separator();
            ui_menu_header(FL_S("Clipboard"));
            ui_menu_item(FL_S("Cut"), .shortcut = FL_S("Ctrl+X"));
            ui_menu_item(FL_S("Copy"), .shortcut = FL_S("Ctrl+C"));
            ui_menu_item(FL_S("Paste"), .shortcut = FL_S("Ctrl+V"));
        }
        ui_menubar_menu(FL_S("View")) {
            ui_menu_check(FL_S("Feature enabled"), &g->checkbox);
            ui_menu_separator();
            ui_menu_header(FL_S("Quality"));
            ui_menu_radio(FL_S("Low quality"), &g->radio_choice, 0);
            ui_menu_radio(FL_S("Medium quality"), &g->radio_choice, 1);
            ui_menu_radio(FL_S("High quality"), &g->radio_choice, 2);
        }
    }
}

static void gallery_basic_sections(GalleryState* g) {
    GALLERY_SECTION {
        gallery_section_header(FL_S("Button"));
        ui_button(FL_S("Click me"));
        const int clen = snprintf(g->clicked_buf, sizeof(g->clicked_buf), "Clicked %d times", g->clicks);
        ui_text((FlString) { .data = g->clicked_buf, .length = (u32)clen });
    }
    GALLERY_SECTION {
        gallery_section_header(FL_S("Checkbox"));
        ui_checkbox(FL_S("Enable feature"), &g->checkbox);
    }
    GALLERY_SECTION {
        gallery_section_header(FL_S("Switch"));
        ui_switch(FL_S("Enable notifications"), &g->toggle);
    }
    GALLERY_SECTION {
        gallery_section_header(FL_S("Radio group"));
        FlString options[] = { FL_S("Low"), FL_S("Medium"), FL_S("High") };
        ui_radio_group(options, (int)sizeof_array(options), &g->radio_choice);
    }
}

static void gallery_input_sections(GalleryState* g) {
    GALLERY_SECTION {
        gallery_section_header(FL_S("Slider"));
        ui_slider_float(FL_S("Volume"), &g->volume, 0.0f, 1.0f);
        ui_slider_int(FL_S("Count"), &g->count, 0, 10);
    }
    GALLERY_SECTION {
        gallery_section_header(FL_S("Progress bar"));
        ui_progress_bar(g->volume);
    }
    GALLERY_SECTION {
        gallery_section_header(FL_S("Dropdown"));
        FlString options[] = { FL_S("Low"), FL_S("Medium"), FL_S("High") };
        ui_dropdown(FL_S("Quality"), &g->dropdown_open, options, (int)sizeof_array(options), &g->dropdown_choice);
    }
    GALLERY_SECTION {
        gallery_section_header(FL_S("Tab bar"));
        FlString tabs[] = { FL_S("General"), FL_S("Audio"), FL_S("Video") };
        ui_tab_bar(tabs, (int)sizeof_array(tabs), &g->tab);
    }
}

static void gallery_overlay_sections(void) {
    GALLERY_SECTION {
        gallery_section_header(FL_S("Tooltip"));
        ui_button(FL_S("Hover for a tip"));
        ui_tooltip(ID("Hover for a tip"), FL_S("Tooltips reveal after a short hover-dwell."));
    }
    GALLERY_SECTION {
        gallery_section_header(FL_S("Message box"));
        ui_button(FL_S("Show alert"));
        ui_button(FL_S("Show confirm"));
    }
    GALLERY_SECTION {
        gallery_section_header(FL_S("Context menu"));
        ui_text(FL_S("Right-click inside the panel below."));
        ui_menu_region(ID("ctx_region"), ID("ctx_menu")) {
            ui_layout_area(.sizing = { .width = sizing_grow() }, .padding = padding_all(24),
                           .color = ui_color(44, 46, 56, 255), .radius = corner_radius(6),
                           .direction = LayoutDirection_TopToBottom) {
                ui_text_styled(FL_S("Right-click here"), .color = ui_color(150, 200, 255, 255));
            }
        }
    }
}

static void gallery_list_sections(GalleryState* g) {
    GALLERY_SECTION {
        gallery_section_header(FL_S("List (virtualized)"));
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(180) },
                       .direction = LayoutDirection_TopToBottom) {
            FlListRange r
                = fl_list_begin(ID("gallery_list"), s_gallery_list_ids, (u32)g->list_rows, g->list_sel,
                                (FlListConfig) { .row_height = 26.0f, .gap = 4.0f, .labels = s_gallery_list_labels });
            for (u32 i = r.start; i < r.end; i++) {
                fl_list_item_begin(s_gallery_list_ids[i]);
                ui_layout_area(.sizing = { .width = sizing_grow() }, .padding = padding_all(6),
                               .radius = corner_radius(4)) {
                    ui_text(s_gallery_list_labels[i]);
                }
                fl_list_item_end();
            }
            fl_list_end();
        }
    }
    GALLERY_SECTION {
        gallery_section_header(FL_S("List (declarative)"));
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(150) },
                       .direction = LayoutDirection_TopToBottom) {
            ui_list(ID("gallery_decl_list"), s_gallery_decl_items, GALLERY_DECL_ROWS, g->decl_sel,
                    (FlListOpts) { .row_height = 26.0f, .gap = 4.0f, .multi_select = true, .reorderable = true });
        }
    }
}

static void gallery_context_menu(void) {
    ui_menu(ID("ctx_menu"), .min_width = 220) {
        ui_menu_item(FL_S("Cut"), .shortcut = FL_S("Ctrl+X"));
        ui_menu_item(FL_S("Copy"), .shortcut = FL_S("Ctrl+C"));
        ui_menu_item(FL_S("Paste"), .shortcut = FL_S("Ctrl+V"), .disabled = true);
        ui_menu_separator();
        ui_menu_item(FL_S("Keep me open"), .close_policy = FlMenuClosePolicy_KeepOpen);
        ui_submenu(FL_S("More")) {
            ui_menu_item(FL_S("Duplicate"));
            ui_menu_item(FL_S("Delete"));
        }
    }
}

static void scene_widget_gallery(FlUiCtx* ctx, int n) {
    render_use_default_font(ctx, 2); // built-in bitmap font, which needs no asset

    GalleryState g = { .clicks = 3,
                       .checkbox = true,
                       .toggle = true,
                       .volume = 0.5f,
                       .count = 5,
                       .dropdown_choice = 1,
                       .radio_choice = 2,
                       .tab = 1 };
    gallery_prepare_lists(&g, n);

    ui_update_begin(ctx);

    ui_message_box(FL_S("Heads up"), FL_S("This is a single-button alert."), &g.alert_open,
                   .confirm_label = FL_S("Got it"));
    ui_message_box(FL_S("Delete item?"), FL_S("This can't be undone."), &g.confirm_open,
                   .confirm_label = FL_S("Delete"), .cancel_label = FL_S("Keep"));

    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = ui_color(20, 21, 27, 255),
                   .direction = LayoutDirection_TopToBottom, .child_gap = 0) {
        gallery_menubar(&g);

        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                       .direction = LayoutDirection_TopToBottom, .child_gap = 16, .padding = padding_all(24)) {
            ui_text_styled(FL_S("flowi — Widget Gallery"), .color = ui_color(235, 235, 245, 255));

            ui_scroll_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                           .direction = LayoutDirection_TopToBottom, .child_gap = 16, .show_scrollbar = true,
                           .smooth_scroll = true) {
                gallery_basic_sections(&g);
                gallery_input_sections(&g);
                gallery_overlay_sections();
                gallery_list_sections(&g);
            }
        }
    }

    gallery_context_menu();

    ui_update_end(ctx, 1.0f / 60.0f);

    selection_set_destroy(g.list_sel);
    selection_set_destroy(g.decl_sel);
}

#undef GALLERY_SECTION

#undef GALLERY_DECL_ROWS

UTEST(perf_fence, scene_widget_gallery) {
    render_fixture_teardown(); // the command count baseline is for a context that has drawn nothing yet
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // 500 is the example's list row count. The list is virtualized to a 180px viewport.
    render_set_layout(ctx, 1920.0f, 1080.0f);
    const FlFrameStats stats = run_perf_scene(ctx, "scene_widget_gallery", scene_widget_gallery, 500);

    ASSERT_EQ_MSG(stats.render_commands, 67u, "render_commands baseline drift");
    ASSERT_EQ_MSG(stats.bin_alloc_bytes, 820080u, "bin_alloc_bytes baseline drift");
    ASSERT_EQ_MSG(stats.clip_broadcasts, 1020u, "clip_broadcasts baseline drift");
}
