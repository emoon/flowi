///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Painter API Implementation
//
// Each painter call draws inside the open layout element's immediate-mode scope: it records the
// neutral render-command IR (ui/render_ir.h) into that element's paint span in the painter's own per-frame
// arena. The first call in a scope lazily attaches the span to the element's Clay CUSTOM marker;
// fl_clay_to_render_ir then splices the span into the frame IR at the element's z.
//
// Colors are raw, geometry is intent, and resources are referenced by handle/buffer; the backend bakes all of
// that on ingest.
//
// All public coordinates use element-local logical (authoring) space and are scaled to actual pixels here, at
// emit time, so span records are in pixel space (the same space Clay output lands in); the splice adds the
// element's resolved rect origin.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "painter.h"

#include <core/arena.h>
#include <core/math.h>
#include "font_cache.h"
#include "image.h"
#include "image_registry.h"
#include "render_ir.h"
#include "render_ir_build.h"

#include <math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Maximum nesting of painting scopes (one per open layout element on the path to the painter call). Deeper
// nesting still balances (push/pop track true depth) but does not record.

#define PAINTER_MAX_SCOPE_DEPTH 64

// Arena reserved size for painter IR (virtual address space, commits on demand)
#define PAINTER_ARENA_INITIAL_SIZE (1024 * 1024 * 1024)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct PaintScope {
    void** custom_slot; // &element->config.custom.customData; the span pointer is written here on first paint
    FlPaintSpan* span;  // null until the first painter call in this scope
    FlEffect effect;    // this scope's current effect; zeroed (kind None) = none. Dies with the scope.
} PaintScope;

struct Painter {
    FlArena* arena;   // Painter's own arena, reset each frame - holds the element-local IR byte stream
    FlArena* scratch; // Per-frame scratch: transient glyph layout AND span/segment bookkeeping
    FlFontCache* font_cache;
    float ui_scale; // Absolute; set by the app via ui_set_scale
    float text_scale;

    PaintScope scope_stack[PAINTER_MAX_SCOPE_DEPTH];
    u32 scope_depth;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline float scale(const Painter* p, float v) {
    return v * p->ui_scale;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Painter* painter_create(FlArena* persistent_arena) {
    Painter* p = arena_alloc_zero(persistent_arena, Painter);
    p->arena = arena_new(.reserved_size = PAINTER_ARENA_INITIAL_SIZE);
    return p;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void painter_destroy(Painter* p) {
    if (p == nullptr) {
        return;
    }
    fl_arena_destroy(p->arena);
    p->arena = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void painter_begin_frame(Painter* p, FlFontCache* fc, float ui_scale, float text_scale, FlArena* scratch) {
    // Spans from the previous frame are dropped with the arena.
    fl_arena_rewind(p->arena);

    p->font_cache = fc;
    p->ui_scale = ui_scale;
    p->text_scale = text_scale > 0.0f ? text_scale : 1.0f;
    p->scratch = scratch;
    p->scope_depth = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Painting scopes. push/pop bracket each layout element; record* funnels every primitive into the innermost
// scope's span, lazily creating it (and attaching the CUSTOM marker) on first use.

void painter_scope_push(Painter* p, void** custom_slot) {
    if (p->scope_depth < PAINTER_MAX_SCOPE_DEPTH) {
        // Zeroed effect: a scope starts with none, so an outer element's effect never reaches this one.
        p->scope_stack[p->scope_depth] = (PaintScope) { .custom_slot = custom_slot, .span = nullptr };
    }
    p->scope_depth++; // count past the cap so pop stays balanced; over-cap scopes simply do not record
}

void painter_scope_pop(Painter* p) {
    if (p->scope_depth > 0) {
        p->scope_depth--;
    }
}

u32 painter_scope_depth(const Painter* p) {
    return p->scope_depth;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Effect state. It lives in the innermost scope's slot, so it dies when that element's scope pops (and with
// every scope at begin_frame) and reaches no sibling, child, or later frame.

void painter_set_effect(Painter* p, FlEffect effect) {
    if (p == nullptr || p->scope_depth == 0 || p->scope_depth > PAINTER_MAX_SCOPE_DEPTH) {
        return;
    }
    p->scope_stack[p->scope_depth - 1].effect = effect;
}

// The effect the next primitive carries, or null when there is none (the builders take NULL for "no effect").
static const FlEffect* painter_current_effect(const Painter* p) {
    if (p->scope_depth == 0 || p->scope_depth > PAINTER_MAX_SCOPE_DEPTH) {
        return nullptr;
    }
    const FlEffect* effect = &p->scope_stack[p->scope_depth - 1].effect;
    return effect->kind != FlEffectKind_None ? effect : nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The innermost scope's span, lazily created and attached to its element's CUSTOM marker. Returns null when
// there is no recordable scope (none open, or nesting past the cap), in which case the primitive no-ops.
static FlPaintSpan* painter_active_span(Painter* p) {
    if (p->scope_depth == 0 || p->scope_depth > PAINTER_MAX_SCOPE_DEPTH) {
        return nullptr;
    }
    PaintScope* s = &p->scope_stack[p->scope_depth - 1];
    if (s->span == nullptr) {
        s->span = arena_alloc_zero(p->scratch, FlPaintSpan);
        if (s->custom_slot != nullptr) {
            *s->custom_slot = s->span; // attach the marker exactly once, on first paint
        }
    }
    return s->span;
}

// Begin recording one primitive: returns the active span (else null → skip the primitive) and 8-aligns the
// arena so the upcoming record starts exactly at *out_start (the byte_size walk requires record-aligned starts).
static FlPaintSpan* painter_record_begin(Painter* p, void** out_start) {
    FlPaintSpan* span = painter_active_span(p);
    if (span == nullptr) {
        return nullptr;
    }
    fl_arena_align(p->arena, 8);
    *out_start = fl_arena_current_pos(p->arena);
    return span;
}

// Finish recording: register [start, current) with the span, coalescing with the previous segment when the new
// record is contiguous.
static void painter_record_end(Painter* p, FlPaintSpan* span, void* start) {
    // A record's actual allocation can be a few bytes short of its 8-rounded byte_size, so align to 8 here to
    // span whole records - matching the byte_size walk the splice uses.
    fl_arena_align(p->arena, 8);
    u8* end = (u8*)fl_arena_current_pos(p->arena);
    u32 size = (u32)(end - (u8*)start);
    if (size == 0) {
        return;
    }
    if (span->last != nullptr && (const u8*)span->last->ir + span->last->size == (const u8*)start) {
        span->last->size += size;
        return;
    }
    FlPaintSegment* seg = arena_alloc(p->scratch, FlPaintSegment);
    seg->ir = start;
    seg->size = size;
    seg->next = nullptr;
    if (span->last != nullptr) {
        span->last->next = seg;
    } else {
        span->first = seg;
    }
    span->last = seg;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Text drawing. Glyphs are laid out here via the shared font cache and emitted as a positioned text run; the
// backend re-resolves each glyph id and rasterizes it.

void painter_text(Painter* p, float x, float y, FlString text, FlFontId font, FlColor color) {
    if (p == nullptr || text.length == 0 || text.data == nullptr || p->font_cache == nullptr) {
        return;
    }

    // A font handle carries its AUTHORED size, so the type scale is multiplied in here to reach the size the
    // glyphs must rasterize at.
    u16 font_size = (u16)max((float)font_cache_get_size(p->font_cache, font) * p->text_scale + 0.5f, 1.0f);

    GlyphList glyph_list = font_cache_get_glyph_list(p->font_cache, font, font_size);
    if (glyph_list.count == 0) {
        return;
    }

    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }

    float cursor_x = scale(p, x);
    float baseline_y = scale(p, y) + (float)glyph_list.ascender / 64.0f;

    // 1:1 scale: the painter draws at the reference-scaled size directly, no separate display zoom.
    fl_ir_text_layout(p->arena, p->scratch, p->font_cache, font, font_cache_whole_px_to_q4(font_size), 1.0f, color,
                      text, cursor_x, baseline_y, 0.0f);
    painter_record_end(p, span, start);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Styled text: the fractional-size (quarter-px quantized) painter draw. Shares the size quantizer, per-glyph
// advances, and the n−1 letter-spacing rule with font_cache_measure_text, so a measured run matches what gets
// drawn. Positions carry ui_scale (chrome); the size and spacing carry text_scale (type).

void painter_text_styled(Painter* p, float x, float y, FlString text, FlTextStyle style, FlColor color) {
    if (p == nullptr || text.length == 0 || text.data == nullptr || p->font_cache == nullptr) {
        return;
    }

    // Silent while the font is still loading/failed - the same metrics gate every measurement entry point
    // applies, so a frame never lands where measure reports real metrics but the draw is skipped.
    if (!font_cache_metrics_ready(p->font_cache, style.font_id)) {
        return;
    }

    // 0 = the font's default size, resolved inside the font layer. style.font_size is authored, so the type
    // scale is multiplied in there too; everything below works in the resulting physical size.
    float size_px = font_cache_resolve_size_px(p->font_cache, style.font_id, style.font_size, p->text_scale);
    u16 size_q4 = font_cache_quantize_size_q4(size_px);
    if (size_q4 == 0) {
        return;
    }

    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }

    // Top-left anchored like painter_text: the baseline sits one ascent below y, at the styled size.
    FlFontMetrics vm = font_cache_get_font_metrics(p->font_cache, style.font_id, size_px);
    float cursor_x = scale(p, x);
    float baseline_y = scale(p, y) + vm.ascent;

    // Letter spacing is authored px; it scales with text_scale.
    fl_ir_text_layout(p->arena, p->scratch, p->font_cache, style.font_id, size_q4, 1.0f, color, text, cursor_x,
                      baseline_y, style.letter_spacing * p->text_scale);
    painter_record_end(p, span, start);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rectangle drawing

// The one fill-rect emitter: every rectangle entry point below funnels through here.
void painter_rect_styled(Painter* p, float x, float y, float w, float h, FlRectConfig config) {
    if (p == nullptr) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }
    float sx = scale(p, x);
    float sy = scale(p, y);
    float sw = scale(p, w);
    float sh = scale(p, h);

    // Radii are lengths, so they scale like the geometry; the backend clamps each to half the smaller
    // dimension (pill shapes).
    FlCornerRadius radius = { scale(p, config.radius.tl), scale(p, config.radius.tr), scale(p, config.radius.bl),
                              scale(p, config.radius.br) };
    fl_ir_fill_rect(p->arena, (FlRect) { sx, sy, sw, sh }, radius, config.corner, painter_current_effect(p));
    painter_record_end(p, span, start);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void painter_rect(Painter* p, float x, float y, float w, float h, FlColor color) {
    painter_rect_styled(p, x, y, w, h, (FlRectConfig) { .corner = { color, color, color, color } });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void painter_rect_rounded(Painter* p, float x, float y, float w, float h, float radius, FlColor color) {
    painter_rect_styled(
        p, x, y, w, h,
        (FlRectConfig) { .radius = { radius, radius, radius, radius }, .corner = { color, color, color, color } });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void painter_gradient_rect(Painter* p, float x, float y, float w, float h, FlColor top_left, FlColor top_right,
                           FlColor bottom_left, FlColor bottom_right) {
    // The IR's gradient is a fill rect with per-corner colors, ordered TL, TR, BL, BR.
    painter_rect_styled(p, x, y, w, h, (FlRectConfig) { .corner = { top_left, top_right, bottom_left, bottom_right } });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 1px lines (separators, grid, rulers). One logical pixel thick, scaled and snapped to a whole device pixel
// (never thinner than 1) here; the line's position is snapped later at the splice, where the absolute device
// coordinate (element origin + scaled local) is known. Emitted as a core fill rect tagged FL_SNAP_DEVICE_PX.

static inline float device_hairline(float ui_scale) {
    float t = roundf(ui_scale); // ui_scale is always > 0
    return t < 1.0f ? 1.0f : t;
}

void painter_hline(Painter* p, float x, float y, float length, FlColor color) {
    if (p == nullptr) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }
    FlRect rect = { scale(p, x), scale(p, y), scale(p, length), device_hairline(p->ui_scale) };
    fl_ir_fill_rect_snapped(p->arena, rect, color, painter_current_effect(p));
    painter_record_end(p, span, start);
}

void painter_vline(Painter* p, float x, float y, float length, FlColor color) {
    if (p == nullptr) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }
    FlRect rect = { scale(p, x), scale(p, y), device_hairline(p->ui_scale), scale(p, length) };
    fl_ir_fill_rect_snapped(p->arena, rect, color, painter_current_effect(p));
    painter_record_end(p, span, start);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Image drawing

bool painter_image(Painter* p, float x, float y, float w, float h, FlImageHandle image, FlColor color) {
    if (p == nullptr) {
        return false;
    }

    // Readiness and native size come from the core image registry: ready == the active backend
    // realized the handle.
    int native_w = 0;
    int native_h = 0;
    if (!image_registry_is_ready((ImageHandle)image, &native_w, &native_h)) {
        return false;
    }

    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return false;
    }

    // A non-positive dimension falls back to the image's native pixel size.
    float sx = scale(p, x);
    float sy = scale(p, y);
    float sw = (w > 0.0f) ? scale(p, w) : (float)native_w;
    float sh = (h > 0.0f) ? scale(p, h) : (float)native_h;

    FlImageSource source = { .kind = FlImageKind_Static };
    source.handle = image;
    fl_ir_image(p->arena, source, FlImageMode_Color, (FlRect) { sx, sy, sw, sh }, (FlRect) { 0 }, color,
                FlImageFilter_Linear, painter_current_effect(p));
    painter_record_end(p, span, start);
    return true;
}

bool painter_image_sub(Painter* p, float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh,
                       FlImageHandle image, FlColor color) {
    if (p == nullptr || sw <= 0.0f || sh <= 0.0f) {
        return false;
    }

    int native_w = 0;
    int native_h = 0;
    if (!image_registry_is_ready((ImageHandle)image, &native_w, &native_h)) {
        return false;
    }

    float src_x0 = clamp(sx, 0.0f, (float)native_w);
    float src_y0 = clamp(sy, 0.0f, (float)native_h);
    float src_x1 = clamp(sx + sw, src_x0, (float)native_w);
    float src_y1 = clamp(sy + sh, src_y0, (float)native_h);
    float src_w = src_x1 - src_x0;
    float src_h = src_y1 - src_y0;
    if (src_w <= 0.0f || src_h <= 0.0f) {
        return false;
    }

    float dst_x = scale(p, dx);
    float dst_y = scale(p, dy);
    float dst_w = (dw > 0.0f) ? scale(p, dw) : src_w;
    float dst_h = (dh > 0.0f) ? scale(p, dh) : src_h;
    if (dst_w <= 0.0f || dst_h <= 0.0f) {
        return false;
    }

    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return false;
    }

    FlImageSource source = { .kind = FlImageKind_Static };
    source.handle = image;
    fl_ir_image(p->arena, source, FlImageMode_Color, (FlRect) { dst_x, dst_y, dst_w, dst_h },
                (FlRect) { src_x0, src_y0, src_w, src_h }, color, FlImageFilter_Linear, painter_current_effect(p));
    painter_record_end(p, span, start);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void painter_dynamic_image(Painter* p, float x, float y, FlDynamicImage* image, FlColor color) {
    if (p == nullptr || image == nullptr || image->user_buffer == nullptr) {
        return;
    }

    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }

    float sx = scale(p, x);
    float sy = scale(p, y);
    float sw = (float)image->width;
    float sh = (float)image->height;

    // The dynamic buffer crosses the IR as a live buffer; the backend deinterleaves/colorizes it.
    FlImageSource source = { .kind = FlImageKind_Dynamic };
    source.dynamic.pixels = image->user_buffer;
    source.dynamic.w = (u32)image->width;
    source.dynamic.h = (u32)image->height;
    source.dynamic.stride = (u32)image->user_stride;

    // In alpha-coverage mode the tint colorizes the mask.
    FlImageMode mode = image->is_alpha_only ? FlImageMode_AlphaCoverage : FlImageMode_Color;
    fl_ir_image(p->arena, source, mode, (FlRect) { sx, sy, sw, sh }, (FlRect) { 0 }, color, FlImageFilter_Linear,
                painter_current_effect(p));
    painter_record_end(p, span, start);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shape drawing (optional-tier IR). Scalar geometry is scaled up front; the trailing arrays (polyline points,
// bars) are scaled while being written directly into the record the builder reserved. Angles and per-point
// intensity are unitless and pass through unscaled.

void painter_polyline(Painter* p, const FlPolyPoint* points, u32 point_count, float width, FlColor color, u32 flags,
                      FlColor fill_color, float baseline_y) {
    if (p == nullptr || points == nullptr || point_count == 0) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }

    FlPolyPoint* dst = fl_ir_polyline(p->arena, color, fill_color, scale(p, width), scale(p, baseline_y), flags,
                                      point_count, painter_current_effect(p));
    for (u32 i = 0; i < point_count; i++) {
        dst[i].x = scale(p, points[i].x);
        dst[i].y = scale(p, points[i].y);
        dst[i].intensity = points[i].intensity;
    }
    painter_record_end(p, span, start);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void painter_arc(Painter* p, float cx, float cy, float radius, float thickness, float start_angle, float sweep,
                 FlColor color, u32 flags) {
    if (p == nullptr) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }
    // Angles are unitless (radians) - only the lengths scale.
    fl_ir_arc(p->arena, scale(p, cx), scale(p, cy), scale(p, radius), scale(p, thickness), start_angle, sweep, color,
              flags, painter_current_effect(p));
    painter_record_end(p, span, start);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void painter_circle_stroked(Painter* p, float cx, float cy, float radius, FlColor color, FlColor border_color,
                            float border_width, FlStrokeStyle stroke) {
    if (p == nullptr) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }
    // The dash pattern is a pair of lengths along the ring, so it scales like the geometry.
    stroke.dash_len = scale(p, stroke.dash_len);
    stroke.gap_len = scale(p, stroke.gap_len);
    fl_ir_circle_stroked(p->arena, scale(p, cx), scale(p, cy), scale(p, radius), color, border_color,
                         scale(p, border_width), stroke, painter_current_effect(p));
    painter_record_end(p, span, start);
}

void painter_circle(Painter* p, float cx, float cy, float radius, FlColor color, FlColor border_color,
                    float border_width) {
    painter_circle_stroked(p, cx, cy, radius, color, border_color, border_width, (FlStrokeStyle) { 0 });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void painter_diamond(Painter* p, float cx, float cy, float rx, float ry, FlColor color, FlColor border_color,
                     float border_width) {
    if (p == nullptr) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }
    fl_ir_diamond(p->arena, scale(p, cx), scale(p, cy), scale(p, rx), scale(p, ry), color, border_color,
                  scale(p, border_width), painter_current_effect(p));
    painter_record_end(p, span, start);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void painter_bars(Painter* p, float baseline_y, const FlBar* bars, u32 bar_count, FlColor color) {
    if (p == nullptr || bars == nullptr || bar_count == 0) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }

    FlBar* dst = fl_ir_bars(p->arena, scale(p, baseline_y), color, bar_count, painter_current_effect(p));
    for (u32 i = 0; i < bar_count; i++) {
        dst[i].x = scale(p, bars[i].x);
        dst[i].w = scale(p, bars[i].w);
        dst[i].h = scale(p, bars[i].h);
    }
    painter_record_end(p, span, start);
}
