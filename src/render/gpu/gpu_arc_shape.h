#pragma once

// GPU backend: arc geometry, shared by translation and the AABB pass. Records normalize once here (the shader
// never remaps angles).

#include "gpu_ir_to_draw_cmd.h"
#include <ui/render_ir.h>

typedef struct GpuArcShape {
    f32 cx;
    f32 cy;
    f32 outer;
    f32 inner;
    f32 start; // knob convention; sweep > 0 (clockwise) after normalization
    f32 sweep;
    bool round_caps;
    bool full;
} GpuArcShape;

// Normalize an arc record. False for a degenerate arc (zero radius, thickness or sweep) that draws nothing.
bool gpu_arc_shape(const FlCmdArc* arc, GpuArcShape* s);

// Sector-tight bounds, not the full circle.
GpuAabb gpu_arc_aabb(const GpuArcShape* s);
