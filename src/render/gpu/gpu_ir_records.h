#pragma once

// GPU backend: what the translation pass needs to know about the layout of IR records - walking a stream,
// locating a record's effect annotation and bounding how many draw commands a record can commit.

#include <ui/render_ir.h>

typedef void (*GpuRecordFn)(void* user, const FlCmdHeader* header);

// Call fn for each record of an IR byte stream, stopping at a zero-size record.
void gpu_ir_walk(const void* ir_commands, size_t ir_bytes, GpuRecordFn fn, void* user);

// Locate a record's effect annotation, or NULL when it carries none. A kind-0 (none) effect reports no effect; a
// kind whose payload size is not known here also reports none, but warns rather than dropping it silently.
const FlEffect* gpu_record_effect(const FlCmdHeader* header);

// The most draw commands a record can ever commit: the fixed-shape kinds one each, a text run one quad per
// glyph, a bar set one rect per bar, a polyline up to one fill trapezoid per open edge plus one stroke capsule
// per edge (<= 2 * point_count). Clip records commit none.
u32 gpu_record_cmd_bound(const FlCmdHeader* header);
