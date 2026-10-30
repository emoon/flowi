#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: neutral render IR -> backend-private draw-command + AABB arrays.
//
// Walks the variable-length FlCmd records of the neutral render IR (ui/render_ir.h) and produces the two parallel
// arrays the compute passes consume verbatim as SSBOs: a fixed-stride draw-command array for the raster pass and an
// AABB array for the tile classification pass.

#include <core/types.h>
#include <stddef.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fixed-stride draw command, uploaded verbatim as a std430 SSBO. The 80-byte stride is the frozen layout contract
// with the raster shader's DrawCmd struct (vec4 + vec4 + uvec4 + 4 x uint + uvec4): the leading 64 bytes are the
// shape, the trailing uvec4 the optional effect annotation every command may carry.

typedef enum GpuDrawKind {
    GpuDraw_Rect = 0,      // fill rect: per-corner radii (0 = square), per-corner colors (solid = four equal)
    GpuDraw_Image = 1,     // atlas image: radius[] carries the source texel rect, color[0] the premultiplied tint
    GpuDraw_Arc = 2,       // ring sector: rect = center + outer/inner radius, radius[] the angle frame
    GpuDraw_Circle = 3,    // filled circle with an optional inward border ring
    GpuDraw_Diamond = 4,   // filled rhombus with an optional inward border ring
    GpuDraw_Border = 5,    // border band: outer rounded rect minus the inner one the shader derives from the widths
    GpuDraw_SlotImage = 6, // large/dynamic image on a sampler slot: radius[] the source texel rect, aux the slot
    GpuDraw_Polyline = 7,  // ONE polyline segment: rect = (p0.x, p0.y, p1.x, p1.y), a stroke capsule or a
                           // fill-to-baseline trapezoid (GPU_DRAW_POLY_STROKE selects)
    GpuDraw_Glyph = 8,     // ONE glyph quad: a sharp box (radius = the source texel rect in the R8 glyph atlas,
                           // x0 y0 x1 y1) sampling coverage and colorizing it with the premultiplied tint in
                           // color[0]
    GpuDraw_Svg = 9,       // ONE monochrome vector-icon quad: the glyph path over the R8 SVG coverage atlas,
                           // colorized by the icon's authored fill unless the record overrides it
    GpuDraw_SvgColor = 10, // ONE multi-color vector-icon quad: the image path over the RGBA8 SVG atlas, sampling
                           // baked premultiplied RGBA and modulating by the white tint in color[0]
} GpuDrawKind;

// GpuDraw_Image flag bits in GpuDrawCmd.flags.
enum {
    GPU_DRAW_IMAGE_NEAREST = 1u << 0, // sample the atlas nearest-neighbor instead of linear
};

// GpuDraw_Arc flag bits in GpuDrawCmd.flags.
enum {
    GPU_DRAW_ARC_ROUND_CAPS = 1u << 0, // round end caps (IR FlArcFlags_round_caps); default is butt
    GPU_DRAW_ARC_FULL = 1u << 1,       // sweep covers the full circle: the shader skips the wedge cut
};

// GpuDraw_Polyline flag bits in GpuDrawCmd.flags. One of the two sub-kinds a polyline segment carries.
enum {
    GPU_DRAW_POLY_STROKE = 1u << 0, // set: a stroke capsule (p0..p1, round caps), classified line-aware.
                                    // clear: a fill-to-baseline trapezoid under the segment, classified by AABB.
};

// Effect annotation, packed into GpuDrawCmd.flags regardless of kind; the shape's own flag bits live in the low
// byte. The effect parameters ride in GpuDrawCmd.effect.
enum {
    GPU_DRAW_EFFECT_SHIFT = 8,
    GPU_DRAW_EFFECT_MASK = 3u << GPU_DRAW_EFFECT_SHIFT, // 0 = none, 1 = drop shadow, 2 = glow
};

// Dash pattern for GpuDraw_Border and GpuDraw_Circle, riding the free high bits of GpuDrawCmd.flags: two 11-bit
// unsigned fixed-point lengths in 1/16-pixel steps, dash in bits 10..20 and gap in 21..31 (the low byte stays the
// per-kind shape bits, 8..9 the effect selector). The shader rescales the period so a whole number of dashes closes
// each edge or the circumference, so only the ratio and the rough size survive. Lengths past the 128px ceiling
// clamp. A zero pattern is solid.
enum {
    GPU_DRAW_DASH_SHIFT = 10,
    GPU_DRAW_DASH_BITS = 11,
    GPU_DRAW_DASH_MASK = (1u << GPU_DRAW_DASH_BITS) - 1u,
    GPU_DRAW_GAP_SHIFT = GPU_DRAW_DASH_SHIFT + GPU_DRAW_DASH_BITS,
    GPU_DRAW_DASH_UNITS_PER_PX = 16,
};

typedef struct GpuDrawCmd {
    f32 rect[4];   // Rect/Image/SlotImage/Border: x, y, w, h in final pixel coordinates (the IR arrives
                   // pre-scaled; Border: the outer rect). Arc: cx, cy, outer radius, inner radius (0 =
                   // filled sector). Circle: cx, cy, radius, border width. Diamond: cx, cy, rx, ry
                   // (half-extents). Polyline: the segment endpoints p0.x, p0.y, p1.x, p1.y.
    f32 radius[4]; // Rect/Border: per-corner outer radius, TL TR BL BR; clamped to [0, half the smaller rect
                   // dimension] here. Image: the source rect in atlas texels, x0 y0 x1 y1. SlotImage: the
                   // source rect in the slot texture's own texels, x0 y0 x1 y1.
                   // Arc: cos/sin of the sweep's bisector knob angle, then sin/cos of the half-aperture -
                   // the shader's wedge frame, precomputed here.
                   // Diamond: radius[0] is the border width.
                   // Polyline: stroke half-width, intensity at p0, intensity at p1, fill baseline_y.
    u32 color[4];  // Rect: packed RGBA8 per corner, TL TR BL BR, R in the low byte (GLSL unpackUnorm4x8
                   // order); global_alpha folded in. Image/SlotImage: color[0] is the tint, premultiplied
                   // (rgb * a) with global_alpha folded; color[1..3] unused.
                   // Arc: color[0]. Circle/Diamond: color[0] fill, color[1] border.
                   // Border: color[0], then the left/right/top side widths as f32 bits (bottom rides
                   // in aux) - the shader derives the inner geometry from these (the IR contract).
                   // Polyline: color[0] is the stroke (capsule) or fill (trapezoid) color; intensity
                   // (in radius) scales its alpha.
    u32 kind;      // GpuDrawKind
    u32 clip;      // index into GpuDrawList.clips (0 = full target); the raster pass cuts coverage against it
    u32 flags;     // per-kind bits (GpuDraw_Image: GPU_DRAW_IMAGE_*, GpuDraw_Arc: GPU_DRAW_ARC_*); 0 for
                   // kinds without flags. SlotImage carries no filter flag - its slot's sampler decides.
                   // Border/Circle additionally carry their dash pattern in the free high bits
                   // (GPU_DRAW_DASH_*), 0 = solid.
    u32 aux;       // per-kind fourth scalar (Border: the bottom side width as f32 bits; SlotImage: the
                   // sampler-slot index, workgroup-uniform); headroom otherwise
    u32 effect[4]; // optional effect (GPU_DRAW_EFFECT_* in flags selects it; 0 in flags = none): offset x, y
                   // and blur radius as f32 bits in [0..2], the packed RGBA8 effect color in [3]. The shader
                   // evaluates it from the shape's own SDF - offset + blurred for a drop shadow, centered +
                   // additive for a glow.
} GpuDrawCmd;

_Static_assert(sizeof(GpuDrawCmd) == 80, "GpuDrawCmd stride is a frozen contract with the raster shader");

// Per-command bounds for the classification pass and per-frame clip rects for the raster pass (both vec4 in
// the shaders): inclusive min, exclusive max.
typedef struct GpuAabb {
    f32 x0;
    f32 y0;
    f32 x1;
    f32 y1;
} GpuAabb;

_Static_assert(sizeof(GpuAabb) == 16, "GpuAabb stride is a frozen contract with the compute shaders");

typedef struct GpuDrawList {
    GpuDrawCmd* cmds;
    GpuAabb* aabbs; // parallel to cmds: aabbs[i] bounds cmds[i], pre-intersected with cmds[i]'s clip rect
    GpuAabb* clips; // deduplicated clip rects referenced by GpuDrawCmd.clip; clips[0] is the full target
    u32 count;
    u32 clip_count; // >= 1 whenever count > 0 (clips[0] always exists)
} GpuDrawList;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Translate an IR byte stream into the draw-command + AABB + clip-rect arrays, all allocated contiguously in arena.
//
// global_alpha:    frame-wide opacity, folded into each command's packed color alphas here (never pre-folded in the
//                  IR). Background commands ignore it.
// target_*:        the root clip rect.
// atlas/glyphs/svg/svg_color/slots: image and glyph resolution sources, each nullable. A record whose source is
//                  null, unresolved, or unprepared is skipped.
// background:      the frame envelope's background state, emitted at the FRONT of the stream so the UI draws over
//                  it: a clear rect (skipped at zero alpha) then up to two fullscreen slot images. A valid prev
//                  signals a crossfade - prev draws opaque and current at crossfade_t, so over-compositing yields
//                  lerp(prev, current, t). Nullable.
//
// Normalizations applied here, so the shaders never see a degenerate case: corner radii clamp to [0, half the
// smaller rect dimension]; a negative arc sweep flips to its positive-clockwise equivalent and a full sweep sets
// GPU_DRAW_ARC_FULL; the arc wedge frame (bisector + half-aperture trig) is precomputed; border and ring widths
// clamp to 0, and widths that consume the shape collapse to a plain fill in the border color. Records with a zero
// or negative radius, extent, thickness, or sweep drop record-by-record, bar sets bar-by-bar.
//
// One IR record can expand to many commands: a text run to one GpuDraw_Glyph per non-empty glyph, a polyline to one
// GpuDraw_Polyline per segment (fill trapezoids first, then stroke capsules, so the fill sits behind the stroke), a
// bar set to one rect per bar. Classification is therefore per glyph and per segment, not over the whole run or
// polyline box. An effect annotation stamps onto every command the record expands to and dilates the classification
// AABB - shifted for a drop shadow, centered for a glow - before the clip intersection.
//
// Clip state is flattened: FlCmd_ClipPush/ClipPop never reach the GPU stream. Every command carries an index into
// the deduplicated clips array (clips[0] = the full target) and its AABB is pre-intersected with that rect, so a
// command clipped down to nothing drops. Unbalanced push/pop is clamped and logged, never UB.
//
// Slot grants run once over the single translation pass, so once every slot is taken the overflowing commands drop
// in stream order and slots->overflow counts them. A fully clipped-out image neither claims a slot nor marks an
// upload.

struct GpuImageAtlas;
struct GpuGlyphAtlas;
struct GpuSvgAtlas;
struct GpuImageSlots;
struct FlBackground;

GpuDrawList gpu_ir_to_draw_cmds(struct FlArena* arena, const void* ir_commands, size_t ir_bytes, u8 global_alpha,
                                u32 target_width, u32 target_height, const struct GpuImageAtlas* atlas,
                                const struct GpuGlyphAtlas* glyphs, struct GpuImageSlots* slots,
                                const struct FlBackground* background, const struct GpuSvgAtlas* svg,
                                const struct GpuSvgAtlas* svg_color);
