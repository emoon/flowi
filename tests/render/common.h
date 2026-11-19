#pragma once

// Shared includes and fixtures for the unit_tests_render_*.c translation units.

#include "unit_tests_common.h"
#include <limits.h>

#include <core/jobsys.h>
#include <flowi/core/memory_tracker.h>
#include "app/host.h"
#include <render/software/sr_render_helpers.h>

#define CANARY_TEXT "capture_canary_text"
#define CANARY_NAME "capture_canary_name"

FlTextMetrics measure(FlFontCache* cache, FlString text, FlFontId font, float size, float spacing);

extern const char k_svg_mono[189];
extern const char k_svg_multi[190];
extern const char k_svg_gradient[323];
extern const unsigned char k_png_red[73];
extern const unsigned char k_png_blue[73];

uint64_t fnv1a_64(const void* data, size_t size);
bool wait_image_ready(ImageHandle handle);
FlImageStatus wait_public_image_terminal(FlImageHandle handle);
void dump_frame(const char* label, const FlPixelReadback* buf);
const u8* readback_rgb_at(const FlPixelReadback* buf, int x, int y);
bool px_near(const FlPixelReadback* buf, int x, int y, FlColor want, int tol);
void fill_dynamic_rgba(FlDynamicImage* img);
void fill_dynamic_alpha(FlDynamicImage* img);

typedef struct AppSmokeState {
    FlColor color;
    bool frame_ran;
    bool frame_str_ok;
    bool frame_str_array_ok;
} AppSmokeState;

void app_smoke_frame(void* user_data);

// A failed ASSERT inside a helper returns from the helper only; the case then returns here.
#define RETURN_IF_FAILED()      \
    do {                        \
        if (*utest_result != 0) \
            return;             \
    } while (0)

#define RENDER_W 1920
#define RENDER_H 1080

extern const FlThemeColors k_known_theme;
extern const FlThemeColors k_skin_theme;

void render_set_layout(FlUiCtx* ctx, f32 width, f32 height);
void render_set_layout_1080p(FlUiCtx* ctx);
void render_mouse_away(FlUiCtx* ctx);
void render_use_default_font(FlUiCtx* ctx, u32 scale);

// Reads the surface back, asserting the backend can and the buffer is populated.
void read_frame(int* utest_result, FlUiCtx* ctx, FlPixelReadback* buf);
// read_frame plus the RENDER_W x RENDER_H dimension check.
void read_frame_1080p(int* utest_result, FlUiCtx* ctx, FlPixelReadback* buf);

typedef struct PxProbe {
    int x;
    int y;
    FlColor color;
    int tol;
    bool present;
    const char* msg;
} PxProbe;

// Asserts every probe in order: px_near(color) must equal present.
void check_px_probes(int* utest_result, const FlPixelReadback* buf, const PxProbe* probes, size_t count);

// The lengths of the runs found in one pixel column. count includes runs past the eight that are stored.
typedef struct RunList {
    int values[8];
    int count;
} RunList;

// Walks column x top to bottom and records the length of each run of pixels equal to rgb.
void column_runs(const FlPixelReadback* buf, int x, const u8 rgb[3], RunList* out);

// A scratch arena recording render IR, with the stream start remembered.
typedef struct IrRecording {
    FlArena* ir;
    const u8* start;
} IrRecording;

IrRecording ir_recording_begin(void);
void ir_recording_end(IrRecording* rec);

// A standalone software backend with one surface.
typedef struct SwTarget {
    FlArena* arena;
    FlRenderBackend backend;
    FlSurface* surface;
} SwTarget;

void sw_target_create(SwTarget* target, u32 width, u32 height);
void sw_target_destroy(SwTarget* target);
typedef struct SwRenderStyle {
    FlColor clear;
    u8 global_alpha;
} SwRenderStyle;

// Renders the recorded IR over an opaque clear color at the given global alpha and reads the surface back.
bool sw_target_render(SwTarget* target, const IrRecording* rec, const SwRenderStyle* style, FlPixelReadback* out);

// A pixel whose r, g and b must each lie within the inclusive [lo, hi] range.
typedef struct PxRange {
    int x;
    int y;
    int lo[3];
    int hi[3];
    const char* msg;
} PxRange;

void check_px_ranges(int* utest_result, const FlPixelReadback* buf, const PxRange* ranges, size_t count);

// A raw-upload handle with the message each of its readiness checks reports.
typedef struct RawUpload {
    FlImageHandle handle;
    const char* invalid_msg;
    const char* ready_msg;
    const char* resident_msg;
} RawUpload;

void check_raw_uploads(int* utest_result, const RawUpload* uploads, size_t count);

// The index-th snapshot render command when it has the given type.
bool render_cmd_at_of_type(int index, UiRenderCommandType type, UiRenderCommand* out);

// Table rows for runs of equality checks. The measured value is evaluated when the table is built.
typedef struct IntCase {
    int got;
    int want;
    const char* msg;
} IntCase;

typedef struct NearCase {
    double got;
    double want;
    double tolerance;
    const char* msg;
} NearCase;

typedef struct BoolCase {
    bool got;
    bool want;
    const char* msg;
} BoolCase;

void check_int_cases(int* utest_result, const IntCase* cases, size_t count);
void check_near_cases(int* utest_result, const NearCase* cases, size_t count);
void check_bool_cases(int* utest_result, const BoolCase* cases, size_t count);

// Whether any rectangle command of the last frame has this background color (alpha ignored).
bool render_has_rect_rgb(FlColor color);

// The non-fatal forms of the table helpers, for cases whose checks are expectations.
void expect_near_cases(int* utest_result, const NearCase* cases, size_t count);
void expect_int_cases(int* utest_result, const IntCase* cases, size_t count);
// Expects each channel of got to be within tolerance of want.
void expect_rgb_near(int* utest_result, FlColor got, FlColor want, float tolerance);
