#pragma once

#include <core/math.h>
#include <core/string.h>
#include <core/types.h>
#include "ui_ids.h"
#include <vfs/vfs.h>
#include "glyph_outline.h"
#include <flowi/ui/font.h>
#include <flowi/ui/text.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared font layer (CONTEXT.md "Font (shared layer)").
//
// Backend-agnostic font handling: FreeType face management, metrics/measurement, shaping, glyph indices, and
// the glyph-outline service. It NEVER rasterizes - a backend realizes coverage itself, from the raster source
// or the glyph outline handed out here.

typedef struct FlFontCache FlFontCache;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-size vertical metrics for a font. count doubles as a readiness flag (0 = font/metrics not ready yet, so
// callers should skip emitting this run for the frame). All distances are 26.6 fixed point.

typedef struct GlyphList {
    int count;
    int ascender;  // baseline to top
    int descender; // baseline to bottom, typically negative
} GlyphList;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum FontStatus {
    FontStatus_Loading,
    FontStatus_Ready,
    FontStatus_Failed,
} FontStatus;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Raster source access: the upstream input a backend needs to realize a font's glyphs itself - the FreeType
// file bytes, or the built-in default font's scale.

typedef enum FlFontRasterKind {
    FlFontRasterKind_None,     // Unknown / not loaded
    FlFontRasterKind_FreeType, // FreeType file bytes
    FlFontRasterKind_Default,  // Built-in 8x8 default font (microknight), realized backend-side
} FlFontRasterKind;

typedef struct FlFontRasterSource {
    FlFontRasterKind kind;
    const u8* font_data; // FreeType: file bytes (valid for the cache lifetime)
    i64 font_data_size;  // FreeType: byte count
    u16 default_scale;   // Default: integer scale of the 8x8 built-in font
} FlFontRasterSource;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlFontCache* font_cache_new(struct FlArena* arena);
void font_cache_destroy(FlFontCache* self);
void font_cache_update(FlFontCache* self);
FlFontId font_cache_load_font(FlFontCache* self, FlVfsMount* mount, FlString relative_path, u16 font_size);
// Load a FreeType font from memory. The bytes are copied; the face is ready synchronously.
FlFontId font_cache_load_font_memory(FlFontCache* self, const u8* data, i64 size, u16 font_size);
bool font_cache_load_fallback(FlFontCache* self, FlVfsMount* mount, FlString relative_path);
// Designate an already-loaded font as the fallback face; font_cache_load_fallback calls this after loading.
void font_cache_set_fallback_font(FlFontCache* self, FlFontId font_id);
FlFontId font_cache_load_default_font(FlFontCache* self, u16 scale);
// Pump I/O until the font leaves FontStatus_Loading. No timeout: a missing or unreadable file resolves to
// FontStatus_Failed, so the wait always terminates.
void font_cache_wait_loaded(FlFontCache* self, FlFontId id);
// Pump I/O until no font is still loading, then return whether all succeeded. Waits on metrics-readiness only;
// glyph rasterization stays render-when-ready.
bool font_cache_ensure_all_loaded(FlFontCache* self);
FontStatus font_cache_get_font_status(FlFontCache* self, FlFontId font_id);
Vec2 font_cache_get_text_size(FlFontCache* self, FlString text, FlFontId font_id, u16 font_size);
GlyphList font_cache_get_glyph_list(FlFontCache* self, FlFontId font_id, u16 font_size);
int font_cache_get_glyph_advance(FlFontCache* self, FlFontId font_id, u16 font_size, u32 codepoint);
int font_cache_get_size(FlFontCache* self, FlFontId font_id);

// Atlas-size quantization, shared so a backend rasterizes at the same size the shared layer measures at.
u16 font_cache_quantize_size(u16 size);

// Quarter-pixel size key for the fractional-size text path (single-line measurement and painter styled
// draw): round(size × 4), so 7.5/9.5/10.5px chrome text resolves exactly.
u16 font_cache_quantize_size_q4(float size);

// The whole-px layout path expressed as a quarter-px key: atlas-bucket quantize first, then scale into the
// quarter-px key space. This is how every whole-px size enters the q4-keyed text pipeline.
u16 font_cache_whole_px_to_q4(u16 font_size);

// The inverse for backends whose rasterizer works in whole pixels: round a quarter-px key to the nearest
// whole px, never below 1. Keys formed by font_cache_whole_px_to_q4 come back as their already-bucketed size
// (quantize exactly once - do NOT re-bucket the result).
u16 font_cache_q4_to_whole_px(u16 size_q4);

// Resolve a styled font size to physical pixels: font_size <= 0 falls back to the font's load size - the
// same rule font_cache_measure_text and font_cache_get_font_metrics apply - then multiplies by scale. Load
// sizes are authored (never pre-scaled). 0 when the font is unknown or the resolved size is unusable.
float font_cache_resolve_size_px(FlFontCache* self, FlFontId font_id, float font_size, float scale);

// Whether the font's metrics are ready - the same gate every measurement entry point applies, exposed so a
// draw path can skip silently while a font is still loading instead of logging per frame.
bool font_cache_metrics_ready(FlFontCache* self, FlFontId font_id);

// Cached advance (26.6) at a quarter-px size key. Never claims the dense ASCII row (that belongs to the
// whole-px layout/measure path); fractional keys are cached in the per-face hashmap, still O(1) once warm.
int font_cache_get_glyph_advance_q4(FlFontCache* self, FlFontId font_id, u16 size_q4, u32 codepoint);

// Single-line measurement at a fractional size (quarter-pixel quantized). Width is summed glyph
// advances plus letter_spacing between glyphs only (n−1 gaps, no trailing gap); vertical fields
// are the line metrics at the quantized size. font_size <= 0 falls back to the font's load size.
// Unknown/unready fonts return zeroed metrics (with a log); missing glyphs measure via the
// fallback face, same as rendering.
FlTextMetrics font_cache_measure_text(FlFontCache* self, FlString text, FlFontId font_id, float font_size,
                                      float letter_spacing);

// String-independent vertical metrics at a fractional size (quarter-pixel quantized), for stable
// vertical centering of dynamic readouts. font_size <= 0 falls back to the font's load size.
// Unknown/unready fonts return zeroed metrics (with a log).
FlFontMetrics font_cache_get_font_metrics(FlFontCache* self, FlFontId font_id, float font_size);

// The glyph index from the face's character map (0 = missing / not ready / not a FreeType face).
u32 font_cache_get_glyph_index(FlFontCache* self, FlFontId font_id, u32 codepoint);

// One codepoint resolved to the face that actually maps it: the primary font, or the fallback face when the
// primary's cmap has no entry - the same decision the advance path applies internally.
// font is the primary id and glyph_index 0 when neither face maps the codepoint (the caller gets .notdef).
typedef struct FontGlyphRef {
    FlFontId font;   // face the index belongs to
    u32 glyph_index; // index within that face (0 = missing / not ready / not a FreeType face)
} FontGlyphRef;

FontGlyphRef font_cache_resolve_glyph(FlFontCache* self, FlFontId font_id, u32 codepoint);
// Glyph outline (CONTEXT.md "Glyph Outline"): the vector form of one glyph, fetched from FreeType without
// rasterizing. Coordinates are float pixels at the loaded size, y-up, relative to the pen position on the baseline.
// Segments are a closed-contour soup: every contour's closing edge is materialized as a Line segment.
// The outline is loaded light-hinted (FT_LOAD_TARGET_LIGHT) at the quarter-px size key size_q4 (whole-px callers
// convert with font_cache_whole_px_to_q4). Segments are allocated from arena; returns false when the font is
// unknown, not ready, not a FreeType face, or the glyph fails to load.
bool font_cache_get_glyph_outline(FlFontCache* self, FlFontId font_id, u16 size_q4, u32 glyph_id, struct FlArena* arena,
                                  FlGlyphOutline* out);

// Raster source access. Returns false if the font id is unknown or the font is not ready yet.
bool font_cache_get_raster_source(FlFontCache* self, FlFontId font_id, FlFontRasterSource* out);
bool font_cache_get_fallback_raster_source(FlFontCache* self, FlFontRasterSource* out);
