// shared fixtures for the unit_tests_render_*.c tests

#include "render/common.h"

// Measures text at a font, size and letter spacing through the cache.
FlTextMetrics measure(FlFontCache* cache, FlString text, FlFontId font, float size, float spacing) {
    return font_cache_measure_text(cache, text,
                                   (FlTextStyle) { .font_id = font, .font_size = size, .letter_spacing = spacing });
}

const char k_svg_mono[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"80\">"
                          "<rect x=\"10\" y=\"10\" width=\"30\" height=\"30\" fill=\"#ff8000\"/>"
                          "<rect x=\"50\" y=\"40\" width=\"20\" height=\"20\" fill=\"#ff8000\"/>"
                          "</svg>";

const char k_svg_multi[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
                           "<rect x=\"10\" y=\"10\" width=\"30\" height=\"30\" fill=\"#ff0000\"/>"
                           "<rect x=\"50\" y=\"50\" width=\"30\" height=\"30\" fill=\"#0000ff\"/>"
                           "</svg>";

const char k_svg_gradient[]
    = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
      "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"100\" y2=\"0\" gradientUnits=\"userSpaceOnUse\">"
      "<stop offset=\"0\" stop-color=\"#ff0000\"/><stop offset=\"1\" stop-color=\"#0000ff\"/>"
      "</linearGradient></defs>"
      "<rect x=\"0\" y=\"0\" width=\"100\" height=\"100\" fill=\"url(#g)\"/></svg>";

// The frame capture owns the strings it returns.
//
// An element's name and text reach Clay as bare pointers into caller memory (ui_text_styled_impl passes
// string.data straight through), and the inspector's readers run between frames, after that memory has been
// reused. The test uses two plain caller buffers in place of a per-frame arena and overwrites them once the
// frame is captured, so a borrowed pointer fails as a wrong value in an ordinary build and not only under ASAN.

// Runs the software renderer over two frames and checks pixels at positions fixed by the geometry:
//   * "primitives": the full primitive set (rect, rounded, border, clip, gradient, static image, dynamic image
//     in RGBA and alpha, painter) over a single background image composited with alpha over the clear color.
//   * "crossfade": a frame with only a background, for the crossfade compositor, which is a separate path that
//     reads neither the background alpha nor the clear color.
//
// image_load_from_memory decodes on a jobs worker thread, so the test waits on the image cache until the decode
// has finished before rendering.

// 4x4 solid-color truecolor PNGs (color type 2, 3 channels) used for the static image and the two background
// crossfade slots.
const unsigned char k_png_red[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x04, 0x08, 0x02, 0x00, 0x00, 0x00, 0x26, 0x93, 0x09, 0x29, 0x00, 0x00, 0x00, 0x10, 0x49,
    0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x38, 0xa1, 0x61, 0x04, 0x47, 0x0c, 0xc4, 0x71, 0x00, 0xe0, 0xe3, 0x12, 0x21,
    0x8b, 0x47, 0xbb, 0xdf, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

const unsigned char k_png_blue[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x04, 0x08, 0x02, 0x00, 0x00, 0x00, 0x26, 0x93, 0x09, 0x29, 0x00, 0x00, 0x00, 0x10, 0x49,
    0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xd0, 0x70, 0x3b, 0x01, 0x47, 0x0c, 0xc4, 0x71, 0x00, 0xee, 0x03, 0x13, 0x61,
    0xe2, 0x06, 0x77, 0xde, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

uint64_t fnv1a_64(const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint64_t hash = 1469598103934665603ULL; // FNV offset basis
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL; // FNV prime
    }
    return hash;
}

// Blocks until an asynchronous in-memory decode is in the image cache.
bool wait_image_ready(ImageHandle handle) {
    for (int i = 0; i < 5000; ++i) {
        if (image_cache_get_data(handle) != nullptr) {
            return true;
        }
        sleep_ms(1);
    }
    return false;
}

FlImageStatus wait_public_image_terminal(FlImageHandle handle) {
    FlImageStatus status = FlImageStatus_Pending;
    for (int i = 0; i < 5000 && status == FlImageStatus_Pending; ++i) {
        status = fl_image_status(handle);
        if (status == FlImageStatus_Pending) {
            sleep_ms(1);
        }
    }
    return status;
}

void write_ppm(const char* path, const u8* rgb24, int width, int height) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    fwrite(rgb24, 1, (size_t)width * (size_t)height * 3u, f);
    fclose(f);
}

// Writes a rendered frame to frame_dump_<label>.ppm when DUMP_FRAMES is set, so a deliberate rendering change can
// be reviewed as an image.
//
// The frames are never compared as a whole. Every case asserts probes computed from the geometry.
void dump_frame(const char* label, const FlPixelReadback* buf) {
    if (getenv("DUMP_FRAMES") == nullptr) {
        return;
    }

    char ppm_path[64];
    snprintf(ppm_path, sizeof ppm_path, "frame_dump_%s.ppm", label);
    write_ppm(ppm_path, (const u8*)buf->pixels, (int)buf->width, (int)buf->height);
}

const u8* readback_rgb_at(const FlPixelReadback* buf, int x, int y) {
    return (const u8*)buf->pixels + (size_t)y * (size_t)buf->stride + (size_t)x * 3u;
}

bool px_near(const FlPixelReadback* buf, int x, int y, FlColor want, int tol) {
    const u8* p = readback_rgb_at(buf, x, y);
    return abs((int)p[0] - (int)want.r) <= tol && abs((int)p[1] - (int)want.g) <= tol
           && abs((int)p[2] - (int)want.b) <= tol;
}

void fill_dynamic_rgba(FlDynamicImage* img) {
    for (int y = 0; y < img->height; ++y) {
        u8* row = img->user_buffer + (size_t)y * (size_t)img->user_stride;
        for (int x = 0; x < img->width; ++x) {
            u8* px = row + (size_t)x * 4u;
            px[0] = (u8)(x * 6);
            px[1] = (u8)(y * 6);
            px[2] = (u8)(160);
            px[3] = 255;
        }
    }
}

void fill_dynamic_alpha(FlDynamicImage* img) {
    for (int y = 0; y < img->height; ++y) {
        u8* row = img->user_buffer + (size_t)y * (size_t)img->user_stride;
        for (int x = 0; x < img->width; ++x) {
            row[x] = (u8)((x ^ y) * 8);
        }
    }
}

// The fl_app headless runner. The case runs the full tick path (pump -> begin -> frame -> end -> present, which
// does nothing here -> input_update), reads a known clear color back through fl_app_read_pixels, and then checks
// the quit flag. fl_app's create calls for the process-global singletons (image system, VFS) do nothing when
// they already exist, and its destroy tears down only what it owns, so the render fixture's globals stay valid.

// Whether fl_frame_str_array(3) reserved three distinct writable slots: each kept the length written to it.
static bool frame_str_array_slots_are_distinct(void) {
    FlString* slots = fl_frame_str_array(3);
    if (slots == nullptr) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        slots[i] = fl_frame_str("abc", (size_t)(i + 1));
    }
    bool distinct = true;
    for (int i = 0; i < 3; i++) {
        const FlString slot = slots[i];
        distinct = distinct && slot.length == (u64)(i + 1);
        distinct = distinct && slot.data != nullptr && slot.data[0] == 'a';
    }
    return distinct;
}

void app_smoke_frame(void* user_data) {
    AppSmokeState* st = (AppSmokeState*)user_data;

    FlString s = fl_frame_str("hi", 2);
    st->frame_str_ok = (s.length == 2 && s.data != nullptr && s.data[0] == 'h' && s.data[1] == 'i');

    // fl_frame_str_array reserves count uninitialized FlString slots in the same frame arena. A zero count gives
    // null and not a one-element allocation.
    st->frame_str_array_ok = frame_str_array_slots_are_distinct() && (fl_frame_str_array(0) == nullptr);

    st->frame_ran = true;

    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = st->color) {}
}

const FlThemeColors k_known_theme = {
    .interactive.base = { 200, 30, 40, 255 },
    .interactive.hover = { 220, 60, 70, 255 },
    .interactive.press = { 170, 20, 30, 255 },
    .interactive.focus = { 255, 255, 255, 255 },
    .interactive.disabled = { 200, 30, 40, 128 },
    .surface.base = { 18, 20, 24, 255 },
    .surface.elevated = { 36, 38, 44, 255 },
    .surface.overlay = { 8, 9, 11, 255 },
    .surface.selected = { 200, 30, 40, 255 },
    .border.default_color = { 50, 52, 60, 255 },
    .border.focus = { 230, 80, 92, 255 },
    .border.separator = { 44, 46, 52, 255 },
    .text.primary = { 240, 240, 240, 255 },
    .text.secondary = { 150, 150, 150, 255 },
    .text.accent = { 200, 30, 40, 255 },
    .feedback.success = { 76, 175, 80, 255 },
    .feedback.warning = { 255, 152, 0, 255 },
    .feedback.error = { 244, 67, 54, 255 },
    .feedback.info = { 33, 150, 243, 255 },
};

const FlThemeColors k_skin_theme = {
    .interactive.base = { 200, 30, 40, 255 },
    .interactive.focus = { 255, 255, 255, 255 },
    .surface.base = { 18, 20, 24, 255 },
    .surface.elevated = { 36, 38, 44, 255 },
    .border.default_color = { 50, 52, 60, 255 },
    .text.primary = { 240, 240, 240, 255 },
    .text.secondary = { 150, 150, 150, 255 },
    .text.accent = { 200, 30, 40, 255 },
};

void render_set_layout(FlUiCtx* ctx, f32 width, f32 height) {
    ctx->layout_width = width;
    ctx->layout_height = height;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = width, .height = height });
}

void render_set_layout_1080p(FlUiCtx* ctx) {
    render_set_layout(ctx, (f32)RENDER_W, (f32)RENDER_H);
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);
}

void render_mouse_away(FlUiCtx* ctx) {
    ctx->input_state.mouse.x = -1000.0f;
    ctx->input_state.mouse.y = -1000.0f;
}

void render_use_default_font(FlUiCtx* ctx, u32 scale) {
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, scale));
}

void read_frame(int* utest_result, FlUiCtx* ctx, FlPixelReadback* buf) {
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf->pixels != nullptr, "render buffer data is null");
}

void read_frame_1080p(int* utest_result, FlUiCtx* ctx, FlPixelReadback* buf) {
    read_frame(utest_result, ctx, buf);
    RETURN_IF_FAILED();
    ASSERT_TRUE_MSG(buf->width == RENDER_W && buf->height == RENDER_H, "unexpected render buffer dimensions");
}

void check_px_probes(int* utest_result, const FlPixelReadback* buf, const PxProbe* probes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const PxProbe* p = &probes[i];
        ASSERT_EQ_MSG(px_near(buf, p->x, p->y, p->color, p->tol), p->present, p->msg);
    }
}

static void run_list_push(RunList* list, int length) {
    if (list->count < (int)sizeof_array(list->values)) {
        list->values[list->count] = length;
    }
    list->count++;
}

void column_runs(const FlPixelReadback* buf, int x, const u8 rgb[3], RunList* out) {
    int current_run = 0;
    for (int y = 0; y < (int)buf->height; y++) {
        if (memcmp(readback_rgb_at(buf, x, y), rgb, 3) == 0) {
            current_run++;
            continue;
        }
        if (current_run > 0) {
            run_list_push(out, current_run);
            current_run = 0;
        }
    }
}

IrRecording ir_recording_begin(void) {
    FlArena* ir = arena_new();
    fl_arena_align(ir, 8);
    return (IrRecording) { .ir = ir, .start = (const u8*)fl_arena_current_pos(ir) };
}

void ir_recording_end(IrRecording* rec) {
    fl_arena_destroy(rec->ir);
    rec->ir = nullptr;
}

void sw_target_create(SwTarget* target, u32 width, u32 height) {
    target->arena = arena_new();
    target->backend = create_software_renderer(target->arena, nullptr, FontRenderMode_Grayscale);
    target->surface = target->backend.create_surface(target->backend.user_data, width, height);
}

void sw_target_destroy(SwTarget* target) {
    target->backend.destroy_surface(target->backend.user_data, target->surface);
    if (target->backend.destroy != nullptr) {
        target->backend.destroy(target->backend.user_data);
    }
    fl_arena_destroy(target->arena);
}

bool sw_target_render(SwTarget* target, const IrRecording* rec, const SwRenderStyle* style, FlPixelReadback* out) {
    const FlRenderFrame frame = {
        .commands = rec->start,
        .bytes = (size_t)((const u8*)fl_arena_current_pos(rec->ir) - rec->start),
        .global_alpha = style->global_alpha,
        .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = style->clear },
    };
    render_seam_render(&target->backend, target->surface, &frame);
    return render_seam_read_pixels(&target->backend, target->surface, out);
}

void check_px_ranges(int* utest_result, const FlPixelReadback* buf, const PxRange* ranges, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const u8* px = readback_rgb_at(buf, ranges[i].x, ranges[i].y);
        const bool in_range = px[0] >= ranges[i].lo[0] && px[0] <= ranges[i].hi[0] && px[1] >= ranges[i].lo[1]
                              && px[1] <= ranges[i].hi[1] && px[2] >= ranges[i].lo[2] && px[2] <= ranges[i].hi[2];
        ASSERT_TRUE_MSG(in_range, ranges[i].msg);
    }
}

void check_raw_uploads(int* utest_result, const RawUpload* uploads, size_t count) {
    for (size_t i = 0; i < count; i++) {
        ASSERT_NE_MSG(uploads[i].handle, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, uploads[i].invalid_msg);
        ASSERT_TRUE_MSG(fl_image_is_ready(uploads[i].handle), uploads[i].ready_msg);
        ASSERT_TRUE_MSG(image_cache_get_data((ImageHandle)uploads[i].handle) != nullptr, uploads[i].resident_msg);
    }
}

bool render_cmd_at_of_type(int index, UiRenderCommandType type, UiRenderCommand* out) {
    return ui_get_render_command(index, out) && out->type == type;
}

void check_int_cases(int* utest_result, const IntCase* cases, size_t count) {
    for (size_t i = 0; i < count; i++) {
        ASSERT_EQ_MSG(cases[i].got, cases[i].want, cases[i].msg);
    }
}

void check_near_cases(int* utest_result, const NearCase* cases, size_t count) {
    for (size_t i = 0; i < count; i++) {
        ASSERT_NEAR_MSG(cases[i].got, cases[i].want, cases[i].tolerance, cases[i].msg);
    }
}

void check_bool_cases(int* utest_result, const BoolCase* cases, size_t count) {
    for (size_t i = 0; i < count; i++) {
        ASSERT_EQ_MSG(cases[i].got, cases[i].want, cases[i].msg);
    }
}

bool render_has_rect_rgb(FlColor color) {
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!render_cmd_at_of_type(i, UiRenderCmd_Rectangle, &cmd)) {
            continue;
        }
        const bool same_rgb = (u8)(cmd.bg_color.r + 0.5f) == color.r && (u8)(cmd.bg_color.g + 0.5f) == color.g
                              && (u8)(cmd.bg_color.b + 0.5f) == color.b;
        if (same_rgb) {
            return true;
        }
    }
    return false;
}

void expect_near_cases(int* utest_result, const NearCase* cases, size_t count) {
    for (size_t i = 0; i < count; i++) {
        EXPECT_NEAR_MSG(cases[i].got, cases[i].want, cases[i].tolerance, cases[i].msg);
    }
}

void expect_int_cases(int* utest_result, const IntCase* cases, size_t count) {
    for (size_t i = 0; i < count; i++) {
        EXPECT_EQ_MSG(cases[i].got, cases[i].want, cases[i].msg);
    }
}

void expect_rgb_near(int* utest_result, FlColor got, FlColor want, float tolerance) {
    EXPECT_NEAR((f32)got.r, (f32)want.r, tolerance);
    EXPECT_NEAR((f32)got.g, (f32)want.g, tolerance);
    EXPECT_NEAR((f32)got.b, (f32)want.b, tolerance);
}
