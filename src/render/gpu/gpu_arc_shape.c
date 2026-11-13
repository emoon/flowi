#include "gpu_arc_shape.h"
#include "gpu_aabb.h"
#include <core/core.h>
#include <math.h>

static const f32 GPU_TAU = 6.28318530717958647692f;

static bool arc_is_degenerate(const FlCmdArc* arc) {
    return arc->radius <= 0.0f || arc->thickness <= 0.0f || arc->sweep == 0.0f;
}

bool gpu_arc_shape(const FlCmdArc* arc, GpuArcShape* s) {
    if (arc_is_degenerate(arc)) {
        return false;
    }
    f32 start = arc->start_angle;
    f32 sweep = arc->sweep;
    if (sweep < 0.0f) {
        start += sweep;
        sweep = -sweep;
    }
    *s = (GpuArcShape) {
        .cx = arc->cx,
        .cy = arc->cy,
        .outer = arc->radius,
        .inner = fmaxf(arc->radius - arc->thickness, 0.0f),
        .start = start,
        .sweep = sweep,
        .round_caps = (arc->flags & FlArcFlags_round_caps) != 0,
        .full = sweep >= GPU_TAU,
    };
    return true;
}

typedef struct Point {
    f32 x;
    f32 y;
} Point;

// The point at a knob-convention angle: 0 at 12 o'clock, positive clockwise, y-down pixel space.
static Point arc_point(const GpuArcShape* s, f32 angle, f32 r) {
    return (Point) { s->cx + r * sinf(angle), s->cy - r * cosf(angle) };
}

static void add_arc_point(GpuAabb* bb, const GpuArcShape* s, f32 angle, f32 r) {
    Point p = arc_point(s, angle, r);
    gpu_aabb_add_point(bb, p.x, p.y);
}

// The endpoint at both radii; round caps add the cap disc at the centerline endpoint.
static void add_end(GpuAabb* bb, const GpuArcShape* s, f32 angle) {
    add_arc_point(bb, s, angle, s->outer);
    add_arc_point(bb, s, angle, s->inner);
    if (s->round_caps) {
        f32 cap_radius = 0.5f * (s->outer - s->inner);
        Point p = arc_point(s, angle, 0.5f * (s->outer + s->inner));
        gpu_aabb_add_point(bb, p.x - cap_radius, p.y - cap_radius);
        gpu_aabb_add_point(bb, p.x + cap_radius, p.y + cap_radius);
    }
}

// Outer-radius cardinal directions (unit steps in x, y) at 12, 3, 6 and 9 o'clock.
static const f32 k_cardinals[4][2] = { { 0.0f, -1.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }, { -1.0f, 0.0f } };

// Cardinal crossings: normalize the start into [0, tau) and walk quarter turns across the sweep. The sweep is
// below tau here (full circles are handled before), so k stays under 8.
static void add_cardinals(GpuAabb* bb, const GpuArcShape* s) {
    f32 a0 = fmodf(s->start, GPU_TAU);
    if (a0 < 0.0f) {
        a0 += GPU_TAU;
    }
    const f32 quarter = 0.25f * GPU_TAU;
    for (int k = (int)ceilf(a0 / quarter); (f32)k * quarter <= a0 + s->sweep; k++) {
        const f32* dir = k_cardinals[k & 3];
        gpu_aabb_add_point(bb, s->cx + dir[0] * s->outer, s->cy + dir[1] * s->outer);
    }
}

// The extremes of {r in [inner, outer], angle in the sweep} are the endpoints at both radii plus the
// outer-radius cardinal directions the sweep crosses (interior maxima of sin/cos; inner-radius cardinals are
// never extreme - the outer one dominates outward and an endpoint dominates inward).
GpuAabb gpu_arc_aabb(const GpuArcShape* s) {
    if (s->full) {
        return (GpuAabb) { s->cx - s->outer, s->cy - s->outer, s->cx + s->outer, s->cy + s->outer };
    }
    Point first = arc_point(s, s->start, s->outer);
    GpuAabb bb = { first.x, first.y, first.x, first.y };
    add_end(&bb, s, s->start);
    add_end(&bb, s, s->start + s->sweep);
    add_cardinals(&bb, s);
    return bb;
}
