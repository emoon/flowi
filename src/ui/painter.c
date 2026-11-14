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

#include "painter.h"

#include <core/arena.h>
#include <core/math.h>
#include "font_cache.h"
#include "image/image.h"
#include "image/image_registry.h"
#include "render_ir.h"
#include "render_ir_build.h"

#include <math.h>

// Maximum nesting of painting scopes (one per open layout element on the path to the painter call). Deeper
// nesting still balances (push/pop track true depth) but does not record.

#define PAINTER_MAX_SCOPE_DEPTH 64

// Arena reserved size for painter IR (virtual address space, commits on demand)
#define PAINTER_ARENA_INITIAL_SIZE (1024 * 1024 * 1024)

typedef struct PaintScope {
    void** custom_slot; // &element->config.custom.customData; the span pointer is written here on first paint
    FlPaintSpan* span;  // null until the first painter call in this scope
    FlEffect effect;    // this scope's current effect; zeroed (kind None) = none. Dies with the scope.
} PaintScope;

struct Painter {
    FlArena* arena;       // Painter's own arena, reset each frame - holds the element-local IR byte stream
    FlArena* state_arena; // Per-frame state: transient glyph layout AND span/segment bookkeeping
    FlFontCache* font_cache;
    float ui_scale; // Absolute; set by the app via ui_set_scale
    float text_scale;

    PaintScope scope_stack[PAINTER_MAX_SCOPE_DEPTH];
    u32 scope_depth;
};

static inline float scale(const Painter* p, float v) {
    return v * p->ui_scale;
}

Painter* painter_create(FlArena* persistent_arena) {
    Painter* p = arena_alloc_zero(persistent_arena, Painter);
    p->arena = arena_new(.reserved_size = PAINTER_ARENA_INITIAL_SIZE);
    return p;
}

void painter_destroy(Painter* p) {
    if (p == nullptr) {
        return;
    }
    fl_arena_destroy(p->arena);
    p->arena = nullptr;
}

void painter_begin_frame(Painter* p, PainterFrame frame) {
    // Spans from the previous frame are dropped with the arena.
    fl_arena_rewind(p->arena);

    p->font_cache = frame.font_cache;
    p->ui_scale = frame.ui_scale;
    p->text_scale = frame.text_scale > 0.0f ? frame.text_scale : 1.0f;
    p->state_arena = frame.state_arena;
    p->scope_depth = 0;
}

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

// Effect state. It lives in the innermost scope's slot, so it dies when that element's scope pops (and with
// every scope at begin_frame) and reaches no sibling, child, or later frame.

// The innermost scope, or null when none is open or nesting is past the cap (such scopes do not record).
static const PaintScope* painter_top_scope(const Painter* p) {
    if (p->scope_depth == 0 || p->scope_depth > PAINTER_MAX_SCOPE_DEPTH) {
        return nullptr;
    }
    return &p->scope_stack[p->scope_depth - 1];
}

// The same scope for the callers that write to it; the slot lives in the non-const painter.
static PaintScope* painter_top_scope_mut(Painter* p) {
    return (PaintScope*)painter_top_scope(p);
}

void painter_set_effect(Painter* p, FlEffect effect) {
    if (p == nullptr) {
        return;
    }
    PaintScope* scope = painter_top_scope_mut(p);
    if (scope != nullptr) {
        scope->effect = effect;
    }
}

// The effect the next primitive carries, or null when there is none (the builders take nullptr for "no effect").
static const FlEffect* painter_current_effect(const Painter* p) {
    const PaintScope* scope = painter_top_scope(p);
    if (scope == nullptr || scope->effect.kind == FlEffectKind_None) {
        return nullptr;
    }
    return &scope->effect;
}

// The innermost scope's span, lazily created and attached to its element's CUSTOM marker. Returns null when
// there is no recordable scope (none open, or nesting past the cap), in which case the primitive no-ops.
static FlPaintSpan* painter_active_span(Painter* p) {
    PaintScope* s = painter_top_scope_mut(p);
    if (s == nullptr) {
        return nullptr;
    }
    if (s->span == nullptr) {
        s->span = arena_alloc_zero(p->state_arena, FlPaintSpan);
        if (s->custom_slot != nullptr) {
            *s->custom_slot = s->span; // attach the marker exactly once, on first paint
        }
    }
    return s->span;
}

// Begin recording one primitive: returns the active span (else null → skip the primitive)
// and 8-aligns the arena so the upcoming record starts exactly at *out_start (the byte_size walk requires
// record-aligned starts).
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
    FlPaintSegment* seg = arena_alloc(p->state_arena, FlPaintSegment);
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

// Text drawing. Glyphs are laid out here via the shared font cache and emitted as a positioned text run; the
// backend re-resolves each glyph id and rasterizes it.

void painter_text(Painter* p, float x, float y, FlString text, FlFontId font, FlColor color) {
    if (p == nullptr) {
        return;
    }
    if (string_is_empty(text) || p->font_cache == nullptr) {
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
    fl_ir_text_layout(p->arena, p->state_arena, p->font_cache, font, font_cache_whole_px_to_q4(font_size), 1.0f, color,
                      text, cursor_x, baseline_y, 0.0f);
    painter_record_end(p, span, start);
}

// Styled text: the fractional-size (quarter-px quantized) painter draw. Shares the size quantizer, per-glyph
// advances, and the n−1 letter-spacing rule with font_cache_measure_text, so a measured run matches what gets
// drawn. Positions carry ui_scale (chrome); the size and spacing carry text_scale (type).

void painter_text_styled(Painter* p, float x, float y, FlString text, FlTextStyle style, FlColor color) {
    if (p == nullptr) {
        return;
    }
    if (string_is_empty(text) || p->font_cache == nullptr) {
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
    fl_ir_text_layout(p->arena, p->state_arena, p->font_cache, style.font_id, size_q4, 1.0f, color, text, cursor_x,
                      baseline_y, style.letter_spacing * p->text_scale);
    painter_record_end(p, span, start);
}

// Rectangle drawing

// The one fill-rect record writer: logical-space geometry and radii are scaled to pixels here, so every rectangle
// entry point (single or batched) emits the same bytes. The caller owns the span bracket.
static void painter_emit_fill_rect(Painter* p, FlRect rect, const FlRectConfig* config, const FlEffect* effect) {
    // Radii are lengths, so they scale like the geometry; the backend clamps each to half the smaller
    // dimension (pill shapes).
    FlCornerRadius radius = config->radius;
    FlCornerRadius scaled = {
        .tl = scale(p, radius.tl), .tr = scale(p, radius.tr), .bl = scale(p, radius.bl), .br = scale(p, radius.br)
    };
    FlRect scaled_rect = { .x = scale(p, rect.x), .y = scale(p, rect.y), .w = scale(p, rect.w), .h = scale(p, rect.h) };
    fl_ir_fill_rect(p->arena, scaled_rect, scaled, config->corner, effect);
}

void painter_rect_styled(Painter* p, FlRect rect, FlRectConfig config) {
    if (p == nullptr) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }
    painter_emit_fill_rect(p, rect, &config, painter_current_effect(p));
    painter_record_end(p, span, start);
}

void painter_rect(Painter* p, FlRect rect, FlColor color) {
    painter_rect_styled(p, rect, (FlRectConfig) { .corner = { color, color, color, color } });
}

void painter_rects(Painter* p, const FlColoredRect* rects, u32 rect_count) {
    if (p == nullptr) {
        return;
    }
    if (rects == nullptr || rect_count == 0) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }
    const FlEffect* effect = painter_current_effect(p);
    for (u32 i = 0; i < rect_count; i++) {
        const FlColoredRect* r = &rects[i];
        const FlRectConfig config = { .corner = { r->color, r->color, r->color, r->color } };
        painter_emit_fill_rect(p, (FlRect) { .x = r->x, .y = r->y, .w = r->w, .h = r->h }, &config, effect);
    }
    painter_record_end(p, span, start);
}

void painter_rect_rounded(Painter* p, FlRect rect, float radius, FlColor color) {
    painter_rect_styled(
        p, rect,
        (FlRectConfig) { .radius = { radius, radius, radius, radius }, .corner = { color, color, color, color } });
}

// A dash pattern is a pair of lengths along the stroke, so it scales like the geometry.
static FlStrokeStyle painter_scale_dash(const Painter* p, FlStrokeStyle dash) {
    return (FlStrokeStyle) { .dash_len = scale(p, dash.dash_len), .gap_len = scale(p, dash.gap_len) };
}

void painter_rect_stroke(Painter* p, FlRect rect, float radius, FlStroke stroke) {
    if (p == nullptr) {
        return;
    }
    // The band cannot be wider than half the rect; past that the outline is the filled rect. Clamping here
    // keeps every backend inside the rect, whatever it does with an over-wide band itself.
    const float width = min(stroke.width, min(rect.w, rect.h) * 0.5f);
    if (!(width > 0.0f)) {
        return; // also rejects an empty or negative rect, and a NaN width
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }
    const float r = scale(p, radius);
    const float w = scale(p, width);
    const FlCmdBorder band = {
        .rect = { .x = scale(p, rect.x), .y = scale(p, rect.y), .w = scale(p, rect.w), .h = scale(p, rect.h) },
        .w_l = w,
        .w_r = w,
        .w_t = w,
        .w_b = w,
        .radius = { .tl = r, .tr = r, .bl = r, .br = r },
        .color = stroke.color,
        .stroke = painter_scale_dash(p, stroke.dash),
    };
    fl_ir_border_cmd(p->arena, &band, painter_current_effect(p));
    painter_record_end(p, span, start);
}

void painter_gradient_rect(Painter* p, FlRect rect, FlCornerColors colors) {
    // The IR's gradient is a fill rect with per-corner colors, ordered TL, TR, BL, BR.
    painter_rect_styled(
        p, rect,
        (FlRectConfig) { .corner = { colors.top_left, colors.top_right, colors.bottom_left, colors.bottom_right } });
}

// 1px lines (separators, grid, rulers). One logical pixel thick, scaled and snapped to a whole device pixel
// (never thinner than 1) here; the line's position is snapped later at the splice, where the absolute device
// coordinate (element origin + scaled local) is known. Emitted as a core fill rect tagged FL_SNAP_DEVICE_PX.

static inline float device_hairline(float ui_scale) {
    float t = roundf(ui_scale); // ui_scale is always > 0
    return t < 1.0f ? 1.0f : t;
}

// Records one snapped fill rect given in pixel space.
static void painter_record_snapped_rect(Painter* p, FlRect rect, FlColor color) {
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }
    fl_ir_fill_rect_snapped(p->arena, rect, color, painter_current_effect(p));
    painter_record_end(p, span, start);
}

void painter_hline(Painter* p, float x, float y, float length, FlColor color) {
    if (p == nullptr) {
        return;
    }
    painter_record_snapped_rect(
        p, (FlRect) { .x = scale(p, x), .y = scale(p, y), .w = scale(p, length), .h = device_hairline(p->ui_scale) },
        color);
}

void painter_vline(Painter* p, float x, float y, float length, FlColor color) {
    if (p == nullptr) {
        return;
    }
    painter_record_snapped_rect(
        p, (FlRect) { .x = scale(p, x), .y = scale(p, y), .w = device_hairline(p->ui_scale), .h = scale(p, length) },
        color);
}

// Image drawing

// A logical length scaled to pixels, or the fallback pixel length when it is not positive.
static float painter_extent_or(const Painter* p, float length, float fallback_px) {
    return length > 0.0f ? scale(p, length) : fallback_px;
}

bool painter_image(Painter* p, FlRect rect, FlImageHandle image, FlColor color) {
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
    FlRect dst = { .x = scale(p, rect.x),
                   .y = scale(p, rect.y),
                   .w = painter_extent_or(p, rect.w, (float)native_w),
                   .h = painter_extent_or(p, rect.h, (float)native_h) };

    FlImageSource source = { .kind = FlImageKind_Static };
    source.handle = image;
    fl_ir_image(p->arena, source, FlImageMode_Color, dst, (FlRect) { 0 }, color, FlImageFilter_Linear,
                painter_current_effect(p));
    painter_record_end(p, span, start);
    return true;
}

// The part of the requested source rect that lies inside a ready image, in image pixels. False when the image
// is not ready or nothing of the rect is left.
static bool painter_ready_source(FlImageHandle image, FlRect want, FlRect* out) {
    int native_w = 0;
    int native_h = 0;
    if (!image_registry_is_ready((ImageHandle)image, &native_w, &native_h)) {
        return false;
    }
    float x0 = clamp(want.x, 0.0f, (float)native_w);
    float y0 = clamp(want.y, 0.0f, (float)native_h);
    float x1 = clamp(want.x + want.w, x0, (float)native_w);
    float y1 = clamp(want.y + want.h, y0, (float)native_h);
    *out = (FlRect) { .x = x0, .y = y0, .w = x1 - x0, .h = y1 - y0 };
    return out->w > 0.0f && out->h > 0.0f;
}

bool painter_image_sub(Painter* p, FlImageSub sub) {
    if (p == nullptr) {
        return false;
    }
    if (sub.sw <= 0.0f || sub.sh <= 0.0f) {
        return false;
    }

    FlRect src;
    if (!painter_ready_source(sub.image, (FlRect) { .x = sub.sx, .y = sub.sy, .w = sub.sw, .h = sub.sh }, &src)) {
        return false;
    }

    FlRect dst = { .x = scale(p, sub.dx),
                   .y = scale(p, sub.dy),
                   .w = painter_extent_or(p, sub.dw, src.w),
                   .h = painter_extent_or(p, sub.dh, src.h) };
    if (dst.w <= 0.0f || dst.h <= 0.0f) {
        return false;
    }

    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return false;
    }

    FlImageSource source = { .kind = FlImageKind_Static };
    source.handle = sub.image;
    fl_ir_image(p->arena, source, FlImageMode_Color, dst, src, sub.color, FlImageFilter_Linear,
                painter_current_effect(p));
    painter_record_end(p, span, start);
    return true;
}

void painter_dynamic_image(Painter* p, FlVec2 pos, FlDynamicImage* image, FlColor color) {
    if (p == nullptr) {
        return;
    }
    if (image == nullptr || image->user_buffer == nullptr) {
        return;
    }

    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }

    FlRect dst = { .x = scale(p, pos.x), .y = scale(p, pos.y), .w = (float)image->width, .h = (float)image->height };

    // The dynamic buffer crosses the IR as a live buffer; the backend deinterleaves/colorizes it.
    FlImageSource source = { .kind = FlImageKind_Dynamic };
    source.dynamic.pixels = image->user_buffer;
    source.dynamic.w = (u32)image->width;
    source.dynamic.h = (u32)image->height;
    source.dynamic.stride = (u32)image->user_stride;

    // In alpha-coverage mode the tint colorizes the mask.
    FlImageMode mode = image->is_alpha_only ? FlImageMode_AlphaCoverage : FlImageMode_Color;
    fl_ir_image(p->arena, source, mode, dst, (FlRect) { 0 }, color, FlImageFilter_Linear, painter_current_effect(p));
    painter_record_end(p, span, start);
}

// Shape drawing (optional-tier IR). Scalar geometry is scaled up front; the trailing arrays (polyline points,
// bars) are scaled while being written directly into the record the builder reserved. Angles and per-point
// intensity are unitless and pass through unscaled.

void painter_polyline(Painter* p, const FlPolyPoint* points, u32 point_count, FlPolylineStyle style) {
    if (p == nullptr) {
        return;
    }
    if (points == nullptr || point_count == 0) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }

    FlPolyPoint* dst = fl_ir_polyline(p->arena, style.stroke.color, style.fill_color, scale(p, style.stroke.width),
                                      scale(p, style.baseline_y), style.flags, point_count, painter_current_effect(p));
    for (u32 i = 0; i < point_count; i++) {
        dst[i].x = scale(p, points[i].x);
        dst[i].y = scale(p, points[i].y);
        dst[i].intensity = points[i].intensity;
    }
    painter_record_end(p, span, start);
}

void painter_line(Painter* p, FlVec2 a, FlVec2 b, FlStroke stroke) {
    if (a.x == b.x && a.y == b.y) {
        // A zero-length segment has no direction for the backend to stroke, so draw what its two round caps
        // add up to: a dot of the stroke width.
        painter_circle(p, a, stroke.width * 0.5f, stroke.color);
        return;
    }
    const FlPolyPoint points[2]
        = { { .x = a.x, .y = a.y, .intensity = 1.0f }, { .x = b.x, .y = b.y, .intensity = 1.0f } };
    painter_polyline(p, points, 2, (FlPolylineStyle) { .stroke = stroke });
}

void painter_arc(Painter* p, FlVec2 center, float radius, float start_angle, float sweep, FlStroke stroke) {
    if (p == nullptr) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }
    // Angles are unitless (radians) - only the lengths scale.
    const u32 flags = stroke.cap == FlStrokeCap_Round ? FL_IR_ARC_ROUND_CAPS : 0u;
    fl_ir_arc(p->arena, scale(p, center.x), scale(p, center.y), scale(p, radius), scale(p, stroke.width), start_angle,
              sweep, stroke.color, flags, painter_current_effect(p));
    painter_record_end(p, span, start);
}

// The fill an outline-only shape carries: fully transparent, so only the ring is drawn. The GPU backend also
// reads a transparent fill as "hollow" when it casts the shape's shadow or glow.
static const FlColor k_no_fill = { 0 };

void painter_circle_bordered(Painter* p, FlVec2 center, float radius, FlColor color, FlStroke border) {
    if (p == nullptr) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }
    fl_ir_circle_stroked(p->arena, scale(p, center.x), scale(p, center.y), scale(p, radius), color, border.color,
                         scale(p, border.width), painter_scale_dash(p, border.dash), painter_current_effect(p));
    painter_record_end(p, span, start);
}

void painter_circle(Painter* p, FlVec2 center, float radius, FlColor color) {
    painter_circle_bordered(p, center, radius, color, (FlStroke) { 0 });
}

void painter_circle_stroke(Painter* p, FlVec2 center, float radius, FlStroke stroke) {
    painter_circle_bordered(p, center, radius, k_no_fill, stroke);
}

void painter_diamond_bordered(Painter* p, FlVec2 center, FlVec2 half_extent, FlColor color, FlStroke border) {
    if (p == nullptr) {
        return;
    }
    void* start;
    FlPaintSpan* span = painter_record_begin(p, &start);
    if (span == nullptr) {
        return;
    }
    fl_ir_diamond(p->arena, scale(p, center.x), scale(p, center.y), scale(p, half_extent.x), scale(p, half_extent.y),
                  color, border.color, scale(p, border.width), painter_current_effect(p));
    painter_record_end(p, span, start);
}

void painter_diamond(Painter* p, FlVec2 center, FlVec2 half_extent, FlColor color) {
    painter_diamond_bordered(p, center, half_extent, color, (FlStroke) { 0 });
}

void painter_diamond_stroke(Painter* p, FlVec2 center, FlVec2 half_extent, FlStroke stroke) {
    painter_diamond_bordered(p, center, half_extent, k_no_fill, stroke);
}

void painter_bars(Painter* p, float baseline_y, const FlBar* bars, u32 bar_count, FlColor color) {
    if (p == nullptr) {
        return;
    }
    if (bars == nullptr || bar_count == 0) {
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
