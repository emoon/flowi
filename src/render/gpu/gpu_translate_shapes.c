#include "gpu_translate.h"
#include "gpu_aabb.h"
#include "gpu_arc_shape.h"
#include "gpu_draw_pack.h"
#include <core/core.h>
#include <math.h>

// Clamp a corner radius to [0, half the smaller rect dimension]. Adjacent clamped radii can then never
// overlap: their sum is at most the smaller dimension, which is at most either side length.
static f32 clamp_radius(f32 radius, f32 max_radius) {
    return fminf(fmaxf(radius, 0.0f), max_radius);
}

// A rounded-rect command with the given per-corner colors; the radii normalize here.
static GpuDrawCmd rect_cmd(FlRect rect, FlCornerRadius radius, const u32 colors[4]) {
    f32 max_radius = fminf(rect.w, rect.h) * 0.5f;
    return (GpuDrawCmd) {
        .rect = { rect.x, rect.y, rect.w, rect.h },
        .radius = { clamp_radius(radius.tl, max_radius), clamp_radius(radius.tr, max_radius),
                    clamp_radius(radius.bl, max_radius), clamp_radius(radius.br, max_radius) },
        .color = { colors[0], colors[1], colors[2], colors[3] },
        .kind = GpuDraw_Rect,
    };
}

static bool has_area(FlRect r) {
    return r.w > 0.0f && r.h > 0.0f;
}

void gpu_translate_fill_rect(GpuTranslate* tr, const void* payload) {
    const FlCmdFillRect* fr = payload;
    if (!has_area(fr->rect)) {
        return;
    }
    u32 colors[4];
    for_count(i, 4) {
        colors[i] = gpu_pack_color(fr->corner[i], tr->global_alpha);
    }
    gpu_emit(tr, gpu_aabb_from_rect(fr->rect), rect_cmd(fr->rect, fr->radius, colors));
}

// Normalized here: negative widths clamp to 0, a record whose widths are all 0 (or whose rect is empty)
// drops, and widths consuming the rect (the inner rect vanishes) become a plain fill in the border color, so
// the shader's outer-minus-inner coverage split never degenerates.
void gpu_translate_border(GpuTranslate* tr, const void* payload) {
    const FlCmdBorder* b = payload;
    f32 w_l = fmaxf(b->w_l, 0.0f);
    f32 w_r = fmaxf(b->w_r, 0.0f);
    f32 w_t = fmaxf(b->w_t, 0.0f);
    f32 w_b = fmaxf(b->w_b, 0.0f);
    if (!has_area(b->rect) || !(w_l + w_r + w_t + w_b > 0.0f)) {
        return;
    }
    u32 color = gpu_pack_color(b->color, tr->global_alpha);
    const u32 solid[4] = { color, color, color, color };
    GpuDrawCmd cmd = rect_cmd(b->rect, b->radius, solid);
    bool consumed = w_l + w_r >= b->rect.w || w_t + w_b >= b->rect.h;
    if (!consumed) {
        cmd.kind = GpuDraw_Border;
        cmd.color[1] = gpu_f32_bits(w_l);
        cmd.color[2] = gpu_f32_bits(w_r);
        cmd.color[3] = gpu_f32_bits(w_t);
        cmd.flags = gpu_pack_dash(b->stroke);
        cmd.aux = gpu_f32_bits(w_b);
    }
    gpu_emit(tr, gpu_aabb_from_rect(b->rect), cmd);
}

void gpu_translate_arc(GpuTranslate* tr, const void* payload) {
    const FlCmdArc* arc = payload;
    GpuArcShape s;
    if (!gpu_arc_shape(arc, &s)) {
        return;
    }
    // The shader's wedge frame: rotate so the sweep's bisector points at 12 o'clock, cut at the half-aperture.
    f32 bisector = s.start + 0.5f * s.sweep;
    f32 aperture = 0.5f * s.sweep;
    gpu_emit(tr, gpu_arc_aabb(&s),
             (GpuDrawCmd) {
                 .rect = { s.cx, s.cy, s.outer, s.inner },
                 .radius = { cosf(bisector), sinf(bisector), sinf(aperture), cosf(aperture) },
                 .color = { gpu_pack_color(arc->color, tr->global_alpha) },
                 .kind = GpuDraw_Arc,
                 .flags = (s.round_caps ? GPU_DRAW_ARC_ROUND_CAPS : 0u) | (s.full ? GPU_DRAW_ARC_FULL : 0u),
             });
}

// Circle/diamond borders are an inward ring. Normalized here: a negative width to 0, and a width consuming
// the whole shape (>= the inradius) to a borderless shape in the border color.
typedef struct RingStyle {
    f32 width;
    FlColor fill;
} RingStyle;

static RingStyle ring_style(f32 border_width, f32 inradius, FlColor fill, FlColor border_color) {
    f32 width = fmaxf(border_width, 0.0f);
    if (width >= inradius) {
        return (RingStyle) { 0.0f, border_color };
    }
    return (RingStyle) { width, fill };
}

void gpu_translate_circle(GpuTranslate* tr, const void* payload) {
    const FlCmdCircle* ci = payload;
    if (ci->radius <= 0.0f) {
        return;
    }
    RingStyle ring = ring_style(ci->border_width, ci->radius, ci->color, ci->border_color);
    GpuAabb bounds = { ci->cx - ci->radius, ci->cy - ci->radius, ci->cx + ci->radius, ci->cy + ci->radius };
    gpu_emit(tr, bounds,
             (GpuDrawCmd) {
                 .rect = { ci->cx, ci->cy, ci->radius, ring.width },
                 .color
                 = { gpu_pack_color(ring.fill, tr->global_alpha), gpu_pack_color(ci->border_color, tr->global_alpha) },
                 .kind = GpuDraw_Circle,
                 // A dash needs a ring to sit on; a circle normalized down to a borderless disc has none.
                 .flags = ring.width > 0.0f ? gpu_pack_dash(ci->stroke) : 0u,
             });
}

void gpu_translate_diamond(GpuTranslate* tr, const void* payload) {
    const FlCmdDiamond* di = payload;
    if (di->rx <= 0.0f || di->ry <= 0.0f) {
        return;
    }
    f32 inradius = di->rx * di->ry / sqrtf(di->rx * di->rx + di->ry * di->ry);
    RingStyle ring = ring_style(di->border_width, inradius, di->color, di->border_color);
    GpuAabb bounds = { di->cx - di->rx, di->cy - di->ry, di->cx + di->rx, di->cy + di->ry };
    gpu_emit(tr, bounds,
             (GpuDrawCmd) {
                 .rect = { di->cx, di->cy, di->rx, di->ry },
                 .radius = { ring.width },
                 .color
                 = { gpu_pack_color(ring.fill, tr->global_alpha), gpu_pack_color(di->border_color, tr->global_alpha) },
                 .kind = GpuDraw_Diamond,
             });
}

// One bar: a solid rect off the shared baseline: a positive height extends up from the baseline, a negative one
// down. Degenerate bars drop.
static void emit_bar(GpuTranslate* tr, const FlCmdBarSet* bs, const FlBar* bar) {
    if (bar->w <= 0.0f || bar->h == 0.0f) {
        return;
    }
    FlRect rect = { bar->x, bar->h >= 0.0f ? bs->baseline_y - bar->h : bs->baseline_y, bar->w, fabsf(bar->h) };
    u32 color = gpu_pack_color(bs->color, tr->global_alpha);
    const u32 solid[4] = { color, color, color, color };
    gpu_emit(tr, gpu_aabb_from_rect(rect), rect_cmd(rect, (FlCornerRadius) { 0 }, solid));
}

// Each bar of a set expands to one solid rect command.
void gpu_translate_bar_set(GpuTranslate* tr, const void* payload) {
    const FlCmdBarSet* bs = payload;
    const FlBar* bars = (const FlBar*)(bs + 1);
    for_count(i, bs->bar_count) {
        emit_bar(tr, bs, &bars[i]);
    }
}

// What one polyline pass paints: the segment command encoding shared by the stroke capsule (p0..p1, round caps,
// half-width, per-endpoint intensities) and the fill-to-baseline trapezoid under the segment. Both use
// rect = (p0.x, p0.y, p1.x, p1.y) and radius = (half_width, i0, i1, baseline); the stroke flag picks the shape
// and, in classification, whether the line-aware narrow phase runs.
typedef struct PolyPaint {
    f32 half_width;
    f32 baseline_y;
    u32 color;
    bool stroke;
} PolyPaint;

static GpuDrawCmd poly_cmd(const FlPolyPoint* a, const FlPolyPoint* b, const PolyPaint* paint) {
    return (GpuDrawCmd) {
        .rect = { a->x, a->y, b->x, b->y },
        .radius = { paint->half_width, a->intensity, b->intensity, paint->baseline_y },
        .color = { paint->color },
        .kind = GpuDraw_Polyline,
        .flags = paint->stroke ? GPU_DRAW_POLY_STROKE : 0u,
    };
}

static void fill_segment(GpuTranslate* tr, const FlPolyPoint* a, const FlPolyPoint* b, const PolyPaint* paint) {
    if (a->x == b->x) {
        return; // a vertical edge spans no x: its trapezoid is empty
    }
    GpuAabb bounds = {
        .x0 = fminf(a->x, b->x),
        .y0 = fminf(fminf(a->y, b->y), paint->baseline_y) - 0.5f,
        .x1 = fmaxf(a->x, b->x),
        .y1 = fmaxf(fmaxf(a->y, b->y), paint->baseline_y) + 0.5f,
    };
    gpu_emit(tr, bounds, poly_cmd(a, b, paint));
}

static void stroke_segment(GpuTranslate* tr, const FlPolyPoint* a, const FlPolyPoint* b, const PolyPaint* paint) {
    if (a->x == b->x && a->y == b->y) {
        return; // a zero-length segment paints nothing (the round cap would be a lone dot)
    }
    f32 pad = paint->half_width + 0.5f; // capsule reach plus the AA ramp
    GpuAabb bounds = {
        .x0 = fminf(a->x, b->x) - pad,
        .y0 = fminf(a->y, b->y) - pad,
        .x1 = fmaxf(a->x, b->x) + pad,
        .y1 = fmaxf(a->y, b->y) + pad,
    };
    gpu_emit(tr, bounds, poly_cmd(a, b, paint));
}

// One fill trapezoid per open edge.
static void fill_pass(GpuTranslate* tr, const FlCmdPolyline* pl, const PolyPaint* paint) {
    const FlPolyPoint* pts = (const FlPolyPoint*)(pl + 1);
    for_count(i, pl->point_count - 1) {
        fill_segment(tr, &pts[i], &pts[i + 1], paint);
    }
}

// One stroke capsule per edge; a closed polyline also strokes the last->first edge.
static void stroke_pass(GpuTranslate* tr, const FlCmdPolyline* pl, const PolyPaint* paint) {
    const FlPolyPoint* pts = (const FlPolyPoint*)(pl + 1);
    bool closed = (pl->flags & FlPolylineFlags_closed) != 0;
    u32 seg_count = closed ? pl->point_count : pl->point_count - 1;
    for_count(i, seg_count) {
        stroke_segment(tr, &pts[i], &pts[(i + 1) % pl->point_count], paint);
    }
}

// Polyline: expand to one segment command per edge. A filled polyline emits its fill-to-baseline trapezoids
// first, then the stroke capsules, so the fill blends behind the stroke. Each stroke segment is a capsule
// with round caps/joins (the SDF's end discs); per-endpoint intensities lerp along it to modulate stroke
// alpha.
void gpu_translate_polyline(GpuTranslate* tr, const void* payload) {
    const FlCmdPolyline* pl = payload;
    if (pl->point_count < 2) {
        return;
    }
    f32 half_width = fmaxf(pl->width, 0.0f) * 0.5f;
    bool filled = (pl->flags & FlPolylineFlags_filled) != 0;
    if (filled) {
        PolyPaint fill = { 0.0f, pl->baseline_y, gpu_pack_color(pl->fill_color, tr->global_alpha), false };
        fill_pass(tr, pl, &fill);
    }
    if (half_width > 0.0f) {
        PolyPaint stroke = { half_width, pl->baseline_y, gpu_pack_color(pl->color, tr->global_alpha), true };
        stroke_pass(tr, pl, &stroke);
    }
}
