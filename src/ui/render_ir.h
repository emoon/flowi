#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// flowi neutral render-command IR.
//
// A single arena of variable-length tagged records describing UI drawing intent - never backend-baked data
// (no SIMD types, no planar glyph bitmaps, no pre-split corners, no pixel pointers for static resources). Each
// backend translates this IR into its own internal form on ingest.
//
// Render-layer types take the Em prefix; they are laid out to pun onto FlColor/FlCornerRadius
// field-for-field for zero-cost conversion (tests/probes/render_ir_compile_check.c pins the layouts equal).

#include <core/types.h>
#include <ui/ui_types.h>
#include <flowi/ui/layout.h>
#include <flowi/core/math_data.h>
#include <flowi/ui/image.h>
#include <flowi/ui/painter.h>
#include <flowi/ui/stroke_style.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared value types
//
// Per-primitive opacity rides in FlColor.a; frame-wide opacity is the frame envelope's global_alpha (see
// render_backend.h), never folded into colors by the emitter.

typedef struct FlRect {
    f32 x; // (x, y, w, h) - not corner-pairs
    f32 y;
    f32 w;
    f32 h;
} FlRect;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command stream
//
// Every record begins with a fixed FlCmdHeader; iterate the arena by advancing byte_size bytes from the
// current record: next = (const u8*)cur + cur->byte_size. The kind selects the payload struct that follows the
// header inline; variable-length payloads (text runs, polylines, bar sets) carry their trailing array
// immediately after the payload. When FL_HAS_EFFECT is set, the FlEffect is appended after the payload and
// any trailing array.

typedef enum FlCmdFlags {
    FL_HAS_EFFECT = 1u << 0, // an FlEffect (defined below) is appended to this record
    // FillRect only: the paint-span splice rounds the rect to whole device pixels after folding in the element
    // origin. No effect on any other command kind.
    FL_SNAP_DEVICE_PX = 1u << 1,
    // bits 2..15 reserved
} FlCmdFlags;

typedef struct FlCmdHeader {
    u16 kind;      // FlCmdKind
    u16 flags;     // FlCmdFlags bitset. global_alpha is NOT per-command - it lives in the frame envelope.
    u32 byte_size; // total bytes of this record (header + payload + any inline trailing data + effect)
} FlCmdHeader;

typedef enum FlCmdKind {
    // Core tier - every backend must render these.
    FlCmd_FillRect,
    FlCmd_Border,
    FlCmd_TextRun,
    FlCmd_Image,
    FlCmd_ClipPush,
    FlCmd_ClipPop,
    // Optional tier - data-driven visualization primitives. A backend MAY ignore these without breaking
    // correctness, layout, or hit-testing; the SW backend skips them.
    FlCmd_Polyline,
    FlCmd_Arc,
    FlCmd_Circle,
    FlCmd_Diamond,
    FlCmd_Bar,
} FlCmdKind;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core-tier payloads

// Fill rect with optional per-corner radii. Gradient is folded in as per-corner colors: a solid fill is four
// equal corner colors. There is no separate gradient command kind.
typedef struct FlCmdFillRect {
    FlRect rect;
    FlCornerRadius radius;
    FlColor corner[4]; // TL, TR, BL, BR
} FlCmdFillRect;

// Border with per-side widths and an outer corner radius. The backend derives the inner geometry from the
// outer radius and the side widths - no pre-split inner_radius crosses the IR. stroke dashes the whole
// band with one pattern (zeroed = solid); it is not positional, so no phase crosses the IR either.
typedef struct FlCmdBorder {
    FlRect rect;
    f32 w_l;
    f32 w_r;
    f32 w_t;
    f32 w_b;
    FlCornerRadius radius; // outer radius
    FlColor color;
    FlStrokeStyle stroke; // dash pattern for the band (zeroed = solid)
} FlCmdBorder;

// One pre-positioned glyph. Shaping and layout happen in the shared font layer, never in a backend; glyph_id
// is opaque to the backend. (x, y) is the pen position at the glyph's baseline.
typedef struct FlGlyph {
    u32 glyph_id;
    f32 x;
    f32 y;
} FlGlyph;

// A run of glyphs in one font/size/color. The FlGlyph[glyph_count] array follows this struct inline in the
// record. Bitmap-font glyphs are NOT text runs - they route through FlCmdImage's alpha-coverage mode.
//
// size is the raster size as a quarter-px fixed-point key (round(px × 4)): the size the glyphs were prepared
// (and the atlas keyed) at, which also fixes each glyph quad's source coverage. scale decouples the display
// size from it - a backend multiplies each glyph quad's placement metrics and extent by scale, so text is
// rasterized crisp at size but shown at `size * scale`. The software backend honors only scale == 1.0.
typedef struct FlCmdTextRun {
    FlFontId font;
    u16 size;
    FlColor color;
    u32 glyph_count; // FlGlyph[glyph_count] inline after this struct
    f32 scale;       // display px = (size / 4) * scale; quad metrics/extent are multiplied by this (1.0 = 1:1)
} FlCmdTextRun;

// Where an image primitive's pixels come from. Stored u8-sized in FlImageSource.kind.
typedef enum FlImageKind {
    FlImageKind_Static = 0,  // cached handle
    FlImageKind_Dynamic = 1, // live neutral buffer realized per frame
} FlImageKind;

// Source of an image primitive: a cached static handle, or a live neutral interleaved-RGBA buffer realized
// per frame.
typedef struct FlImageSource {
    u8 kind; // FlImageKind
    union {
        FlImageHandle handle; // kind == FlImageKind_Static
        struct {
            void* pixels; // kind == FlImageKind_Dynamic: interleaved RGBA (or alpha-only, see FlCmdImage.mode)
            u32 w;
            u32 h;
            u32 stride; // bytes per row
        } dynamic;
    };
} FlImageSource;

// How an image's samples are interpreted. Stored u8-sized in FlCmdImage.mode.
typedef enum FlImageMode {
    FlImageMode_Color = 0,         // RGBA; tint modulates (white tint = none)
    FlImageMode_AlphaCoverage = 1, // single-channel mask; tint colorizes (bitmap glyphs, alpha-only buffers)
} FlImageMode;

// One image primitive, parameterized by source (static vs dynamic) and mode (color vs alpha-coverage). The
// alpha-coverage mode is the IR home for bitmap-font glyphs and alpha-only dynamic images.
typedef struct FlCmdImage {
    FlImageSource source;
    u8 mode;    // FlImageMode
    FlRect dst; // destination rectangle
    FlRect src; // source sub-rect (atlas cell); {0, 0, 0, 0} = whole source
    FlColor tint;
    u8 filter; // FlImageFilter
} FlCmdImage;

// Rect-only clip. FlCmd_ClipPop has no payload (header only).
typedef struct FlCmdClipPush {
    FlRect rect;
} FlCmdClipPush;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Optional-tier payloads (data-driven visualization primitives)
//
// Same conventions as the core tier: f32 pixel coordinates, raw non-premultiplied FlColor, intent only. A
// backend that ignores these kinds skips the whole record via byte_size, so trailing arrays and effects need
// no per-kind knowledge to step over.

// FlPolyPoint (one polyline vertex: x, y, intensity), FlPolylineFlags and FlArcFlags are the generated types from
// painter.def - included above via <flowi/ui/painter.h>.

// Polyline - waveforms, oscilloscopes, filter/EQ response, envelope/LFO editors, wireframe meshes. The
// FlPolyPoint[point_count] array follows this struct inline in the record. Caps and joins are always round -
// a policy, not a field.
typedef struct FlCmdPolyline {
    FlColor color;      // stroke
    FlColor fill_color; // used when FILLED
    f32 width;          // stroke width, pixels
    f32 baseline_y;     // fill between curve and this y when FILLED
    u32 flags;          // FlPolylineFlags
    u32 point_count;    // FlPolyPoint[point_count] inline after this struct
} FlCmdPolyline;

// Arc / ring / circle-sector - knob indicators, modulation rings. One shape: outer radius plus inward
// thickness; thickness >= radius reaches the center and makes a filled sector. Angles follow the knob
// convention: zero at 12 o'clock, positive sweep clockwise.
typedef struct FlCmdArc {
    f32 cx;
    f32 cy;
    f32 radius;      // outer
    f32 thickness;   // inward; >= radius -> filled sector
    f32 start_angle; // radians, 0 = 12 o'clock
    f32 sweep;       // radians, positive = clockwise
    FlColor color;
    u32 flags; // FlArcFlags
} FlCmdArc;

// Circle - modulation points, automation nodes. Filled, with an optional border ring. A hollow ring alone is
// Arc's job.
typedef struct FlCmdCircle {
    f32 cx;
    f32 cy;
    f32 radius;
    FlColor color;        // fill
    FlColor border_color; // used when border_width > 0
    f32 border_width;     // 0 = no border
    FlStrokeStyle stroke; // dash pattern for the border ring (zeroed = solid)
} FlCmdCircle;

// Diamond - field-for-field parallel to Circle with per-axis half-extents.
typedef struct FlCmdDiamond {
    f32 cx;
    f32 cy;
    f32 rx; // half-extents
    f32 ry;
    FlColor color;        // fill
    FlColor border_color; // used when border_width > 0
    f32 border_width;     // 0 = no border
} FlCmdDiamond;

// FlBar (one bar: x, w, h - height from the set's baseline_y, negative draws downward; per-bar width supports
// log-spaced spectrum bins) is the generated type from painter.def - included above via <flowi/ui/painter.h>.

// Bar set - meters, spectrum analyzers, step sequencers. A batched command: one record carries a whole row of
// bars off a shared baseline. The FlBar[bar_count] array follows this struct inline in the record.
typedef struct FlCmdBarSet {
    f32 baseline_y;
    FlColor color;
    u32 bar_count; // FlBar[bar_count] inline after this struct
} FlCmdBarSet;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Effect annotation
//
// A decorative, additive property appended to a command's record - after the payload and any inline trailing
// array - when FL_HAS_EFFECT is set in the header flags. One effect per command. Affects appearance only -
// never geometry or hit-testing; a backend that does not support effects skips it. Drop-shadow and glow share
// one shape (glow = centered + additive).
//
// FlEffect (kind, color, off_x, off_y, blur) and FlEffectKind are the generated types from painter.def -
// included above via <flowi/ui/painter.h>.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Paint span - a deferred chunk of element-local IR
//
// The painter records primitives for one painting scope (one layout element) into a span. A span is a list of
// byte segments: when a child element paints between two of its parent's painter calls, the child's records
// land between the parent's in the shared painter arena, so the parent's span is non-contiguous. A span is referenced
// from its element's Clay CUSTOM command via customData; fl_clay_to_render_ir splices each span's records into the
// frame IR at that command's position - at the element's z, under its children, bracketed by the live layout scissor
// stack. Span records are in element-local pixel space; the splice translates them by the element's resolved rect
// origin.

typedef struct FlPaintSegment {
    const void* ir;              // first FlCmd record of this contiguous run (8-aligned)
    u32 size;                    // bytes in this run (a multiple of 8; whole records only)
    struct FlPaintSegment* next; // next run of the same span, in paint order
} FlPaintSegment;

typedef struct FlPaintSpan {
    FlPaintSegment* first;
    FlPaintSegment* last;
} FlPaintSpan;
