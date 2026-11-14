#pragma once

// GPU backend: per-frame translation state and the per-kind translators that turn one IR record's payload into
// draw commands. Each translator takes the record's payload struct (the FlCmd* named in its comment) and commits
// zero or more commands through gpu_emit.

#include "gpu_clip_stack.h"
#include "gpu_ir_to_draw_cmd.h"
#include <ui/render_backend.h>
#include <ui/render_ir.h>

struct GpuGlyphAtlas;
struct GpuImageAtlas;
struct GpuImageSlots;
struct GpuSvgAtlas;

// n is the running count of surviving commands and indexes the next free slot in out's pre-sized arrays.
typedef struct GpuTranslate {
    GpuDrawList* out;
    u32 n;
    GpuClipStack stack;
    const struct GpuImageAtlas* atlas;
    const struct GpuGlyphAtlas* glyphs;
    const struct GpuSvgAtlas* svg;       // monochrome R8 SVG coverage atlas
    const struct GpuSvgAtlas* svg_color; // multi-color RGBA8 SVG atlas
    struct GpuImageSlots* slots;
    u8 global_alpha;
    const FlEffect* effect;         // the current record's effect (nullptr = none), set around each record's dispatch
    const FlBackground* background; // the frame's background state, emitted ahead of the IR (nullptr = none)
} GpuTranslate;

// The survival gate every kind funnels through: a command whose clip-intersected bounds are empty is dropped
// entirely. Otherwise the command commits with its clip index filled in, and its AABB is the clipped bounds.
void gpu_emit(GpuTranslate* tr, GpuAabb bounds, GpuDrawCmd cmd);

// Pre-test of the survival gate without committing a command: lets the image paths refuse to claim a sampler
// slot (or mark a dynamic upload) for a command that is fully clipped out.
bool gpu_clip_visible(const GpuTranslate* tr, GpuAabb bounds);

// Solid shapes (gpu_translate_shapes.c).
void gpu_translate_fill_rect(GpuTranslate* tr, const void* payload); // FlCmdFillRect
void gpu_translate_border(GpuTranslate* tr, const void* payload);    // FlCmdBorder
void gpu_translate_arc(GpuTranslate* tr, const void* payload);       // FlCmdArc
void gpu_translate_circle(GpuTranslate* tr, const void* payload);    // FlCmdCircle
void gpu_translate_diamond(GpuTranslate* tr, const void* payload);   // FlCmdDiamond
void gpu_translate_bar_set(GpuTranslate* tr, const void* payload);   // FlCmdBarSet
void gpu_translate_polyline(GpuTranslate* tr, const void* payload);  // FlCmdPolyline

// Atlas and slot sampled content (gpu_translate_sampled.c).
void gpu_translate_image(GpuTranslate* tr, const void* payload);    // FlCmdImage
void gpu_translate_text_run(GpuTranslate* tr, const void* payload); // FlCmdTextRun
