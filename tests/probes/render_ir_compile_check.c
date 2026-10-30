///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compile-only check for the neutral render-command IR and the FlRenderBackend interface.
//
// This translation unit includes both IR headers with ZERO consumers and is built (by CMakeLists.txt) with
// only the base + ui-core include directories - no renderer/software or renderer/gpu on the include path and
// nothing linked. If either header grows a dependency on a concrete backend, this stops compiling.
//
// Offsets reflect the natural-alignment LP64 layout the backends compile against.

#include <stddef.h>

#include <ui/render_backend.h>
#include "render_ir.h"

// The shared font layer is core-tier and must stay free of any concrete-backend dependency. Including it here -
// where the build deliberately omits renderer/software from the include path - turns that rule into a compile gate.
#include "font_cache.h"

// Lock the shared layer's metrics-only GlyphList shape (count doubles as the readiness flag; no glyph array). This
// also keeps the include above load-bearing so the header-isolation compile gate cannot be silently dropped.
_Static_assert(sizeof(GlyphList) == 3 * sizeof(int), "GlyphList is metrics-only {count, ascender, descender}");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

_Static_assert(sizeof(FlRect) == 4 * sizeof(f32), "FlRect is exactly {x, y, w, h}");
_Static_assert(offsetof(FlRect, x) == 0, "FlRect.x first");
_Static_assert(offsetof(FlRect, y) == sizeof(f32), "FlRect.y");
_Static_assert(offsetof(FlRect, w) == 2 * sizeof(f32), "FlRect.w");
_Static_assert(offsetof(FlRect, h) == 3 * sizeof(f32), "FlRect.h");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command stream - fixed header, variable-length records.

_Static_assert(sizeof(FlCmdHeader) == 8, "FlCmdHeader is u16 kind + u16 flags + u32 byte_size");
_Static_assert(offsetof(FlCmdHeader, kind) == 0, "FlCmdHeader.kind first");
_Static_assert(offsetof(FlCmdHeader, byte_size) == 4, "FlCmdHeader.byte_size after kind+flags");
_Static_assert(FL_HAS_EFFECT == 1u, "FL_HAS_EFFECT is bit 0");

_Static_assert(FlCmd_FillRect == 0, "core kind ordinals are frozen");
_Static_assert(FlCmd_Border == 1, "core kind ordinals are frozen");
_Static_assert(FlCmd_TextRun == 2, "core kind ordinals are frozen");
_Static_assert(FlCmd_Image == 3, "core kind ordinals are frozen");
_Static_assert(FlCmd_ClipPush == 4, "core kind ordinals are frozen");
_Static_assert(FlCmd_ClipPop == 5, "core kind ordinals are frozen");
_Static_assert(FlCmd_Polyline == 6, "optional tier follows the core tier");
_Static_assert(FlCmd_Arc == 7, "optional tier reserves five slots");
_Static_assert(FlCmd_Circle == 8, "optional tier reserves five slots");
_Static_assert(FlCmd_Diamond == 9, "optional tier reserves five slots");
_Static_assert(FlCmd_Bar == 10, "optional tier reserves five slots");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FillRect - gradient folded in as four per-corner colors (solid = four equal corners).

_Static_assert(sizeof(FlCmdFillRect) == 48, "FillRect is rect + radius + four corner colors only");
_Static_assert(offsetof(FlCmdFillRect, rect) == 0, "FillRect.rect");
_Static_assert(offsetof(FlCmdFillRect, radius) == sizeof(FlRect), "FillRect.radius");
_Static_assert(offsetof(FlCmdFillRect, corner) == sizeof(FlRect) + sizeof(FlCornerRadius), "FillRect.corner");
_Static_assert(sizeof(((FlCmdFillRect*)0)->corner) == 4 * sizeof(FlColor), "FillRect carries four corner colors");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Border - outer radius + per-side widths (backend derives the inner geometry; no pre-split inner_radius).

_Static_assert(sizeof(FlCmdBorder) == 60, "Border is rect + 4 widths + radius + color + stroke only");
_Static_assert(offsetof(FlCmdBorder, rect) == 0, "Border.rect");
_Static_assert(offsetof(FlCmdBorder, w_l) == sizeof(FlRect), "Border.w_l");
_Static_assert(offsetof(FlCmdBorder, w_r) == sizeof(FlRect) + sizeof(f32), "Border.w_r");
_Static_assert(offsetof(FlCmdBorder, w_t) == sizeof(FlRect) + 2 * sizeof(f32), "Border.w_t");
_Static_assert(offsetof(FlCmdBorder, w_b) == sizeof(FlRect) + 3 * sizeof(f32), "Border.w_b");
_Static_assert(offsetof(FlCmdBorder, radius) == sizeof(FlRect) + 4 * sizeof(f32), "Border.radius (outer)");
_Static_assert(offsetof(FlCmdBorder, color) == offsetof(FlCmdBorder, radius) + sizeof(FlCornerRadius), "Border.color");
_Static_assert(offsetof(FlCmdBorder, stroke) == offsetof(FlCmdBorder, color) + sizeof(FlColor),
               "Border.stroke (zeroed = solid)");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TextRun - FlFontId + pre-positioned opaque glyphs (never bitmaps); FlGlyph[glyph_count] trails inline.

_Static_assert(sizeof(FlGlyph) == sizeof(u32) + 2 * sizeof(f32), "FlGlyph is exactly {glyph_id, x, y}");
_Static_assert(offsetof(FlGlyph, glyph_id) == 0, "FlGlyph.glyph_id first (opaque id)");
_Static_assert(offsetof(FlGlyph, x) == sizeof(u32), "FlGlyph.x (baseline pen pos)");
_Static_assert(offsetof(FlGlyph, y) == sizeof(u32) + sizeof(f32), "FlGlyph.y (baseline pen pos)");

_Static_assert(sizeof(FlCmdTextRun) == 20, "TextRun is font + size + color + glyph_count + scale");
_Static_assert(offsetof(FlCmdTextRun, font) == 0, "TextRun.font (FlFontId)");
_Static_assert(sizeof(((FlCmdTextRun*)0)->font) == sizeof(FlFontId), "TextRun references a font by FlFontId");
_Static_assert(offsetof(FlCmdTextRun, size) == sizeof(FlFontId), "TextRun.size (raster/atlas size)");
_Static_assert(offsetof(FlCmdTextRun, color) == sizeof(FlFontId) + sizeof(u16), "TextRun.color");
_Static_assert(offsetof(FlCmdTextRun, glyph_count) == 12, "TextRun.glyph_count governs inline-array iteration");
_Static_assert(offsetof(FlCmdTextRun, scale) == 16, "TextRun.scale (display = size * scale; quad scale factor)");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

_Static_assert(sizeof(FlImageSource) == 32, "FlImageSource is kind + {handle | dynamic} union");
_Static_assert(offsetof(FlImageSource, kind) == 0, "FlImageSource.kind discriminant first");
_Static_assert(offsetof(FlImageSource, handle) == offsetof(FlImageSource, dynamic), "static/dynamic share the union");
_Static_assert(sizeof(((FlImageSource*)0)->handle) == sizeof(FlImageHandle), "static images carry FlImageHandle");

_Static_assert(sizeof(FlCmdImage) == 80, "Image is source + mode + dst + src + tint + filter only");
_Static_assert(offsetof(FlCmdImage, source) == 0, "Image.source");
_Static_assert(offsetof(FlCmdImage, mode) == sizeof(FlImageSource), "Image.mode (0 color, 1 alpha-coverage)");
_Static_assert(offsetof(FlCmdImage, dst) == 36, "Image.dst");
_Static_assert(offsetof(FlCmdImage, src) == 36 + sizeof(FlRect), "Image.src (atlas sub-rect)");
_Static_assert(offsetof(FlCmdImage, tint) == 36 + 2 * sizeof(FlRect), "Image.tint");
_Static_assert(offsetof(FlCmdImage, filter) == 36 + 2 * sizeof(FlRect) + sizeof(FlColor), "Image.filter");

// Enumerator values are protocol: backends branch on these (mode == alpha-coverage, filter == nearest) and
// the dynamic/static discriminant selects the union arm. Pin them so a reorder can't silently shift behavior.
_Static_assert(FlImageKind_Static == 0 && FlImageKind_Dynamic == 1, "image source discriminant values");
_Static_assert(FlImageMode_Color == 0 && FlImageMode_AlphaCoverage == 1, "image mode values");
_Static_assert(FlImageFilter_Linear == 0 && FlImageFilter_Nearest == 1, "image filter values");

// Compile-level proof that both union arms and the alpha-coverage+tint mode exist by name.
static void fl_ir_image_source_shapes(void) {
    FlImageSource s;
    s.kind = FlImageKind_Static;
    s.handle = (FlImageHandle)0;
    s.kind = FlImageKind_Dynamic;
    s.dynamic.pixels = NULL;
    s.dynamic.w = s.dynamic.h = s.dynamic.stride = 0;
    (void)s;

    FlCmdImage img = { 0 };
    img.mode = FlImageMode_AlphaCoverage;
    img.filter = FlImageFilter_Nearest;
    (void)img;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clip is rect-only; ClipPop has no payload (header only).

_Static_assert(sizeof(FlCmdClipPush) == sizeof(FlRect), "clip push is rect-only");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Optional-tier payloads - intent only; a backend MAY ignore these kinds entirely (the SW backend does).
// Trailing arrays (FlPolyPoint, FlBar) follow their payload inline, like text-run glyphs.

_Static_assert(sizeof(FlPolyPoint) == 12, "FlPolyPoint is exactly {x, y, intensity} at a fixed 12B stride");
_Static_assert(offsetof(FlPolyPoint, x) == 0, "FlPolyPoint.x first");
_Static_assert(offsetof(FlPolyPoint, y) == sizeof(f32), "FlPolyPoint.y");
_Static_assert(offsetof(FlPolyPoint, intensity) == 2 * sizeof(f32), "FlPolyPoint.intensity always present");

_Static_assert(FlPolylineFlags_filled == 1u, "polyline FILLED is bit 0");
_Static_assert(FlPolylineFlags_closed == 2u, "polyline CLOSED is bit 1");

_Static_assert(sizeof(FlCmdPolyline) == 24, "Polyline is colors + width + baseline + flags + count only");
_Static_assert(offsetof(FlCmdPolyline, color) == 0, "Polyline.color (stroke)");
_Static_assert(offsetof(FlCmdPolyline, fill_color) == sizeof(FlColor), "Polyline.fill_color");
_Static_assert(offsetof(FlCmdPolyline, width) == 8, "Polyline.width");
_Static_assert(offsetof(FlCmdPolyline, baseline_y) == 12, "Polyline.baseline_y (fill target)");
_Static_assert(offsetof(FlCmdPolyline, flags) == 16, "Polyline.flags");
_Static_assert(offsetof(FlCmdPolyline, point_count) == 20, "Polyline.point_count governs inline-array iteration");

_Static_assert(FlArcFlags_round_caps == 1u, "arc ROUND_CAPS is bit 0 (default butt)");

_Static_assert(sizeof(FlCmdArc) == 32, "Arc is center + radius + thickness + angles + color + flags only");
_Static_assert(offsetof(FlCmdArc, cx) == 0, "Arc.cx");
_Static_assert(offsetof(FlCmdArc, cy) == sizeof(f32), "Arc.cy");
_Static_assert(offsetof(FlCmdArc, radius) == 2 * sizeof(f32), "Arc.radius (outer)");
_Static_assert(offsetof(FlCmdArc, thickness) == 3 * sizeof(f32), "Arc.thickness (inward)");
_Static_assert(offsetof(FlCmdArc, start_angle) == 4 * sizeof(f32), "Arc.start_angle (0 = 12 o'clock)");
_Static_assert(offsetof(FlCmdArc, sweep) == 5 * sizeof(f32), "Arc.sweep (positive = clockwise)");
_Static_assert(offsetof(FlCmdArc, color) == 6 * sizeof(f32), "Arc.color");
_Static_assert(offsetof(FlCmdArc, flags) == 28, "Arc.flags");

_Static_assert(sizeof(FlCmdCircle) == 32, "Circle is center + radius + fill + border color/width + stroke only");
_Static_assert(offsetof(FlCmdCircle, cx) == 0, "Circle.cx");
_Static_assert(offsetof(FlCmdCircle, cy) == sizeof(f32), "Circle.cy");
_Static_assert(offsetof(FlCmdCircle, radius) == 2 * sizeof(f32), "Circle.radius");
_Static_assert(offsetof(FlCmdCircle, color) == 12, "Circle.color (fill)");
_Static_assert(offsetof(FlCmdCircle, border_color) == 16, "Circle.border_color");
_Static_assert(offsetof(FlCmdCircle, border_width) == 20, "Circle.border_width (0 = no border)");
_Static_assert(offsetof(FlCmdCircle, stroke) == 24, "Circle.stroke (zeroed = solid ring)");

_Static_assert(sizeof(FlCmdDiamond) == 28, "Diamond is center + half-extents + fill + border color/width only");
_Static_assert(offsetof(FlCmdDiamond, cx) == 0, "Diamond.cx");
_Static_assert(offsetof(FlCmdDiamond, cy) == sizeof(f32), "Diamond.cy");
_Static_assert(offsetof(FlCmdDiamond, rx) == 2 * sizeof(f32), "Diamond.rx (half-extent)");
_Static_assert(offsetof(FlCmdDiamond, ry) == 3 * sizeof(f32), "Diamond.ry (half-extent)");
_Static_assert(offsetof(FlCmdDiamond, color) == 16, "Diamond.color (fill)");
_Static_assert(offsetof(FlCmdDiamond, border_color) == 20, "Diamond.border_color");
_Static_assert(offsetof(FlCmdDiamond, border_width) == 24, "Diamond.border_width (0 = no border)");

_Static_assert(sizeof(FlBar) == 12, "FlBar is exactly {x, w, h}");
_Static_assert(offsetof(FlBar, x) == 0, "FlBar.x");
_Static_assert(offsetof(FlBar, w) == sizeof(f32), "FlBar.w (per-bar width: log-spaced bins)");
_Static_assert(offsetof(FlBar, h) == 2 * sizeof(f32), "FlBar.h (from baseline up; negative = down)");

_Static_assert(sizeof(FlCmdBarSet) == 12, "BarSet is baseline + color + count only (batched command)");
_Static_assert(offsetof(FlCmdBarSet, baseline_y) == 0, "BarSet.baseline_y (shared by all bars)");
_Static_assert(offsetof(FlCmdBarSet, color) == sizeof(f32), "BarSet.color");
_Static_assert(offsetof(FlCmdBarSet, bar_count) == 8, "BarSet.bar_count governs inline-array iteration");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Effect annotation - flag-gated, appended to a record after the payload and any inline trailing array;
// drop-shadow and glow share one shape.

_Static_assert(sizeof(FlEffect) == 20, "FlEffect is kind + color + off_x + off_y + blur only");
_Static_assert(offsetof(FlEffect, kind) == 0, "FlEffect.kind (0 none, 1 shadow, 2 glow)");
_Static_assert(offsetof(FlEffect, color) == sizeof(u8), "FlEffect.color");
_Static_assert(offsetof(FlEffect, off_x) == 8, "FlEffect.off_x");
_Static_assert(offsetof(FlEffect, off_y) == 8 + sizeof(f32), "FlEffect.off_y");
_Static_assert(offsetof(FlEffect, blur) == 8 + 2 * sizeof(f32), "FlEffect.blur");

// Effect-kind values are protocol: the GPU translator shifts the kind into the draw-command flags and the
// bounds-expansion path distinguishes drop-shadow (offset) from glow (centered). Pin them.
_Static_assert(FlEffectKind_None == 0 && FlEffectKind_DropShadow == 1 && FlEffectKind_Glow == 2, "effect kind values");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame envelope - global_alpha is a frame parameter, never per-command; background travels as state.

_Static_assert(offsetof(FlRenderFrame, global_alpha) > offsetof(FlRenderFrame, bytes), "global_alpha is frame-wide");
_Static_assert(sizeof(((FlRenderFrame*)0)->background) == sizeof(FlBackground), "frame carries background state");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Backend interface - one object; presentation excluded; readback is nullable.

_Static_assert(sizeof(FlRenderBackend) == 17 * sizeof(void*),
               "FlRenderBackend is user_data + 15 function pointers + capability flag");

static bool fl_ir_backend_shapes(void) {
    FlRenderBackend b = { 0 };
    b.user_data = NULL;
    b.create_surface = NULL;
    b.destroy_surface = NULL;
    b.change_resolution = NULL;
    b.render = NULL;
    b.destroy = NULL;
    b.scale_changed = NULL;  // scale-change atlas-regen seam - nullable (GPU regenerates atlases; SW is a no-op)
    b.set_font_cache = NULL; // font-cache host-swap seam - nullable (GPU leaves it unbound)
    b.prepare_image = NULL;
    b.release_image = NULL;
    b.prepare_glyph = NULL;
    b.prepare_vector = NULL;        // vector preparation seam - nullable (only the GPU backend binds it)
    b.read_pixels = NULL;           // optional capability - nullable
    b.prepare_image_on_job = false; // threading capability - unset means the begin-frame pump (GL/D3D)

    FlRenderFrame frame = { 0 };
    frame.commands = NULL;
    frame.bytes = 0;
    frame.global_alpha = 255;
    frame.background.crossfade_t = 0.0f;
    (void)frame;
    return b.read_pixels == NULL;
}

// Reference the helpers so -Wunused-function stays quiet without changing behavior.
void fl_render_ir_compile_check(void);
void fl_render_ir_compile_check(void) {
    fl_ir_image_source_shapes();
    (void)fl_ir_backend_shapes();
}
