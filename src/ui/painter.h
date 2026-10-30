#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Painter API - Low-level drawing primitives for custom rendering
//
// Direct drawing commands for custom effects (gradients, text shadows, dynamic/bitmap images, the
// optional-tier shapes) that aren't easily expressed through the standard widget system.
//
// Painter calls draw inside the open layout element's immediate-mode scope: each call records
// into that element's paint span, and fl_clay_to_render_ir splices the span into the frame at the element's
// Clay CUSTOM marker - at the element's z, under its child elements, bracketed by the live layout scissor
// stack. A painter call outside any open scope is a no-op (see ui_get_painter).
//
// All coordinates are element-local logical (authoring) space and are scaled to actual pixels here, at emit
// time; the splice adds the element's resolved rect origin.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <core/types.h>
#include "render_ir.h"
#include <ui/ui_types.h>
#include <flowi/ui/text.h>
#include <stddef.h>

struct FlArena;
struct FlFontCache;

typedef struct Painter Painter;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lifecycle (internal use only)

Painter* painter_create(struct FlArena* arena);

// Releases the painter's IR arena. The Painter itself lives in the arena passed to painter_create.
void painter_destroy(Painter* p);

// Call before any painter operation this frame; resets the painter's internal arena.
// ui_scale / text_scale: absolute, set by the app via ui_set_scale - font handles carry
// authored sizes, so text draws multiply text_scale in themselves; <= 0 means 1.0.
// state_arena: per-frame state - can be the same arena used for Clay state.
void painter_begin_frame(Painter* p, struct FlFontCache* fc, float ui_scale, float text_scale,
                         struct FlArena* state_arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Painting scopes (internal use only - driven by the layout begin/end seam)
//
// A scope is pushed/popped in lockstep with each layout element's begin/end. The first painter call inside an
// open scope lazily creates that element's paint span and attaches it to the element's Clay CUSTOM marker via
// custom_slot; subsequent calls append to the same span - one marker per scope, never per primitive. An
// empty scope attaches nothing, so non-painting elements incur no CUSTOM command.

// Push a painting scope. custom_slot is the address of the open layout element's CUSTOM customData field;
// the painter writes the span pointer through it on the scope's first paint. May be null (scope records but
// attaches to nothing - e.g. an element the splice will not reach).
void painter_scope_push(Painter* p, void** custom_slot);

void painter_scope_pop(Painter* p);

// Number of open painting scopes. ui_get_painter() is valid only when this is > 0.
u32 painter_scope_depth(const Painter* p);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Effect state - the painter's only state, everything else (colors, geometry) stays per-call.

// Set the effect every later primitive in the innermost open scope carries. A zeroed effect
// (kind FlEffectKind_None) clears it. The state lives in that scope, so it is gone once the scope pops and at
// begin_frame - it can never reach a sibling element, a nested child, or the next frame. Text draws ignore it:
// no backend resolves an effect on a text run.
void painter_set_effect(Painter* p, FlEffect effect);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Text drawing

// Draw text with its top-left at (x, y), in the font's authored size.
void painter_text(Painter* p, float x, float y, FlString text, FlFontId font, FlColor color);

// Draw single-line styled text: style carries the font (already resolved - no 0-means-active here), a
// fractional pixel size (0 = the font's default size), and letter spacing (n−1 gaps, no trailing gap).
// Sizes quantize to quarter-px steps, the same quantizer font_cache_measure_text uses, so a measured run
// matches what gets drawn.
void painter_text_styled(Painter* p, float x, float y, FlString text, FlTextStyle style, FlColor color);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rectangle drawing

// Draw a rectangle styled by config: per-corner radius and per-corner colors at once. This is the single
// fill-rect emitter - the three narrower calls below are thin configs over it.
void painter_rect_styled(Painter* p, float x, float y, float w, float h, FlRectConfig config);

void painter_rect(Painter* p, float x, float y, float w, float h, FlColor color);

void painter_rect_rounded(Painter* p, float x, float y, float w, float h, float radius, FlColor color);

// Draw a rectangle with a per-corner color gradient. The four colors are bilinearly interpolated across the
// quad (top-left, top-right, bottom-left, bottom-right).
void painter_gradient_rect(Painter* p, float x, float y, float w, float h, FlColor top_left, FlColor top_right,
                           FlColor bottom_left, FlColor bottom_right);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Crisp 1px lines (separators, grid lines, rulers)
//
// A hairline one logical pixel thick, scaled by the composed UI scale and snapped to the device-pixel grid so it
// stays sharp at fractional scroll offsets and fractional per-monitor DPI. Emitted as a core FillRect
// (FlCmd_FillRect), not the optional-tier polyline. (x, y) and length are element-local logical (authoring)
// space; the position snap happens at the splice, once the element origin is folded in.

// Horizontal hairline running +length along x from (x, y).
void painter_hline(Painter* p, float x, float y, float length, FlColor color);

// Vertical hairline running +length along y from (x, y).
void painter_vline(Painter* p, float x, float y, float length, FlColor color);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Image drawing

// Draw an image with its top-left at (x, y), scaled to w x h. A non-positive w/h uses the image's native
// pixel size for that dimension. Returns false if the image is not ready/invalid.
bool painter_image(Painter* p, float x, float y, float w, float h, FlImageHandle image, FlColor color);

// Draw a source sub-rectangle of an image. The destination rect is logical (authoring) space and a
// non-positive dw/dh uses sw/sh; the source rect is image pixels. Returns false if image/source is not
// ready/valid.
bool painter_image_sub(Painter* p, float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh,
                       FlImageHandle image, FlColor color);

// Draw a dynamic image (RGBA or alpha-only) with its top-left at (x, y). For alpha-only images color
// colorizes the alpha mask; for RGBA it tints the image (white = no tint).
void painter_dynamic_image(Painter* p, float x, float y, FlDynamicImage* image, FlColor color);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shape drawing (optional-tier IR)
//
// These emit optional-tier render commands: a backend MAY ignore them without breaking correctness, layout,
// or hit-testing (the software backend currently does). Coordinates, radii, and widths are logical
// (authoring) space, scaled at emit like everything else.

// Draw a polyline.
// points: vertices in logical (authoring) space; each point's intensity modulates the stroke alpha (1.0 = plain),
//         lerped along segments, and scales the fill alpha when filled
// width: stroke width; caps and joins are always round
// flags: FlPolylineFlags - FILLED fills between the curve and baseline_y with fill_color, CLOSED adds a
//        last->first segment
void painter_polyline(Painter* p, const FlPolyPoint* points, u32 point_count, float width, FlColor color, u32 flags,
                      FlColor fill_color, float baseline_y);

// Draw an arc / ring / circle-sector.
// thickness: extends inward from radius; thickness >= radius makes a filled sector
// start_angle, sweep: radians; 0 = 12 o'clock, positive sweep = clockwise (knob convention)
// flags: FlArcFlags - ROUND_CAPS rounds the arc ends (default butt caps keep sector pie edges clean)
void painter_arc(Painter* p, float cx, float cy, float radius, float thickness, float start_angle, float sweep,
                 FlColor color, u32 flags);

// Draw a filled circle with an optional border ring (border_width 0 = no border).
void painter_circle(Painter* p, float cx, float cy, float radius, FlColor color, FlColor border_color,
                    float border_width);

// painter_circle with the border ring dashed by stroke (zeroed = solid). Dash lengths are logical px and
// scale with the painter like every other length.
void painter_circle_stroked(Painter* p, float cx, float cy, float radius, FlColor color, FlColor border_color,
                            float border_width, FlStrokeStyle stroke);

// Draw a filled diamond with half-extents rx, ry and an optional border ring (border_width 0 = no border).
void painter_diamond(Painter* p, float cx, float cy, float rx, float ry, FlColor color, FlColor border_color,
                     float border_width);

// Draw a batched row of bars off a shared baseline. Bar x/w/h are logical (authoring) space; a negative h
// draws downward from the baseline.
void painter_bars(Painter* p, float baseline_y, const FlBar* bars, u32 bar_count, FlColor color);
