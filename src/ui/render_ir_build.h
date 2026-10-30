#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// flowi neutral render-command IR - emit helpers.
//
// Small inline builders that append a tagged FlCmd record (render_ir.h) to an arena, producing one contiguous,
// backend-agnostic byte buffer. Records carry drawing intent only - colors are NOT premultiplied and the
// frame's global_alpha is NOT folded in here (both happen backend-side).
//
// Each record is a header immediately followed by its payload. FlCmdImage embeds a void* (FlImageSource), so
// records must start on an 8-byte boundary; every record is therefore started 8-aligned (fl_arena_align below)
// and its byte_size is rounded up to a multiple of 8. That keeps the walk contract literal -
// next = (const u8*)cur + cur->byte_size always lands on the next 8-aligned record. A whole IR buffer can be
// appended to another by copying its bytes to an 8-aligned destination.

#include <core/arena.h>
#include <core/memory.h>
#include <core/utf8.h>
#include "font_cache.h"
#include "render_ir.h"
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start a record: 8-align, emit the header, and stamp byte_size rounded up to the 8-byte record stride.
// total_bytes is the WHOLE record size (header + payload + any inline trailing data), not just the payload.

static inline FlCmdHeader* fl_ir_header(FlArena* arena, FlCmdKind kind, u32 total_bytes) {
    fl_arena_align(arena, 8); // records start 8-aligned (FlCmdImage embeds a void*)
    FlCmdHeader* header = arena_alloc_zero(arena, FlCmdHeader);
    header->kind = (u16)kind;
    header->flags = 0;
    header->byte_size = (total_bytes + 7u) & ~7u;
    // The record owns all byte_size bytes including the rounding pad, which is never allocated; unpoison the
    // whole span so ASAN accepts a memcpy of byte_size bytes.
    asan_unpoison_memory_region(header, header->byte_size);
    return header;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Append the optional effect annotation to the record being built (after the payload and any inline trailing
// array) and mark the header. NULL means no effect. The caller's total_bytes must already include the effect.

static inline u32 fl_ir_effect_bytes(const FlEffect* effect) {
    return effect != nullptr ? (u32)sizeof(FlEffect) : 0u;
}

static inline void fl_ir_append_effect(FlArena* arena, FlCmdHeader* header, const FlEffect* effect) {
    if (effect == nullptr) {
        return;
    }
    header->flags |= FL_HAS_EFFECT;
    FlEffect* dst = arena_alloc(arena, FlEffect);
    *dst = *effect;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fill rect with optional per-corner radii and per-corner colors (a solid fill is four equal corner colors;
// a gradient is four differing ones - there is no separate gradient command). NULL effect = none.

static inline void fl_ir_fill_rect(FlArena* arena, FlRect rect, FlCornerRadius radius, const FlColor corners[4],
                                   const FlEffect* effect) {
    u32 content = (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdFillRect)) + fl_ir_effect_bytes(effect);
    FlCmdHeader* header = fl_ir_header(arena, FlCmd_FillRect, content);
    FlCmdFillRect* cmd = arena_alloc_zero(arena, FlCmdFillRect);
    cmd->rect = rect;
    cmd->radius = radius;
    cmd->corner[0] = corners[0];
    cmd->corner[1] = corners[1];
    cmd->corner[2] = corners[2];
    cmd->corner[3] = corners[3];
    fl_ir_append_effect(arena, header, effect);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device-snapped solid fill rect for crisp 1px lines (separators, grid, rulers). The same record as
// fl_ir_fill_rect but tagged FL_SNAP_DEVICE_PX, so the paint-span splice rounds it to the device-pixel grid after
// adding the element origin. Solid color, no corner radius.

static inline void fl_ir_fill_rect_snapped(FlArena* arena, FlRect rect, FlColor color, const FlEffect* effect) {
    u32 content = (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdFillRect)) + fl_ir_effect_bytes(effect);
    FlCmdHeader* header = fl_ir_header(arena, FlCmd_FillRect, content);
    header->flags |= FL_SNAP_DEVICE_PX;
    FlCmdFillRect* cmd = arena_alloc_zero(arena, FlCmdFillRect);
    cmd->rect = rect;
    cmd->corner[0] = color;
    cmd->corner[1] = color;
    cmd->corner[2] = color;
    cmd->corner[3] = color;
    fl_ir_append_effect(arena, header, effect);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Border with per-side widths and an outer radius (the backend derives the inner geometry). The _stroked
// variant additionally dashes the band; the plain one is that with a zeroed (solid) pattern.

static inline void fl_ir_border_stroked(FlArena* arena, FlRect rect, f32 w_l, f32 w_r, f32 w_t, f32 w_b,
                                        FlCornerRadius radius, FlColor color, FlStrokeStyle stroke) {
    fl_ir_header(arena, FlCmd_Border, (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdBorder)));
    FlCmdBorder* cmd = arena_alloc_zero(arena, FlCmdBorder);
    cmd->rect = rect;
    cmd->w_l = w_l;
    cmd->w_r = w_r;
    cmd->w_t = w_t;
    cmd->w_b = w_b;
    cmd->radius = radius;
    cmd->color = color;
    cmd->stroke = stroke;
}

static inline void fl_ir_border(FlArena* arena, FlRect rect, f32 w_l, f32 w_r, f32 w_t, f32 w_b, FlCornerRadius radius,
                                FlColor color) {
    fl_ir_border_stroked(arena, rect, w_l, w_r, w_t, w_b, radius, color, (FlStrokeStyle) { 0 });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// One image primitive (static handle or dynamic neutral buffer; color or alpha-coverage mode). NULL effect = none.

static inline void fl_ir_image(FlArena* arena, FlImageSource source, FlImageMode mode, FlRect dst, FlRect src,
                               FlColor tint, FlImageFilter filter, const FlEffect* effect) {
    u32 content = (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdImage)) + fl_ir_effect_bytes(effect);
    FlCmdHeader* header = fl_ir_header(arena, FlCmd_Image, content);
    FlCmdImage* cmd = arena_alloc_zero(arena, FlCmdImage);
    cmd->source = source;
    cmd->mode = (u8)mode;
    cmd->dst = dst;
    cmd->src = src;
    cmd->tint = tint;
    cmd->filter = (u8)filter;
    fl_ir_append_effect(arena, header, effect);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A run of pre-positioned glyphs in one font/size/color. The glyphs are copied inline after the payload.

static inline void fl_ir_text_run(FlArena* arena, FlFontId font, u16 size, f32 scale, FlColor color,
                                  const FlGlyph* glyphs, u32 glyph_count) {
    u32 content = (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdTextRun)) + glyph_count * (u32)sizeof(FlGlyph);
    fl_ir_header(arena, FlCmd_TextRun, content);
    FlCmdTextRun* cmd = arena_alloc_zero(arena, FlCmdTextRun);
    cmd->font = font;
    cmd->size = size;
    cmd->color = color;
    cmd->glyph_count = glyph_count;
    cmd->scale = scale;
    FlGlyph* dst = arena_alloc_array(arena, FlGlyph, glyph_count);
    if (glyph_count > 0) {
        memcpy(dst, glyphs, glyph_count * sizeof(FlGlyph));
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rect-only clip push / pop.

static inline void fl_ir_clip_push(FlArena* arena, FlRect rect) {
    fl_ir_header(arena, FlCmd_ClipPush, (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdClipPush)));
    FlCmdClipPush* cmd = arena_alloc_zero(arena, FlCmdClipPush);
    cmd->rect = rect;
}

static inline void fl_ir_clip_pop(FlArena* arena) {
    fl_ir_header(arena, FlCmd_ClipPop, (u32)sizeof(FlCmdHeader));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Optional-tier builders. All five take an optional effect (NULL = none), appended after the payload and any
// inline trailing array. The variable-length builders (polyline, bars) reserve the trailing array inside the
// record and return it; the caller MUST fill every reserved entry.

static inline FlPolyPoint* fl_ir_polyline(FlArena* arena, FlColor color, FlColor fill_color, f32 width, f32 baseline_y,
                                          u32 flags, u32 point_count, const FlEffect* effect) {
    u32 content = (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdPolyline)) + point_count * (u32)sizeof(FlPolyPoint)
                  + fl_ir_effect_bytes(effect);
    FlCmdHeader* header = fl_ir_header(arena, FlCmd_Polyline, content);
    FlCmdPolyline* cmd = arena_alloc_zero(arena, FlCmdPolyline);
    cmd->color = color;
    cmd->fill_color = fill_color;
    cmd->width = width;
    cmd->baseline_y = baseline_y;
    cmd->flags = flags;
    cmd->point_count = point_count;
    FlPolyPoint* points = arena_alloc_array(arena, FlPolyPoint, point_count);
    fl_ir_append_effect(arena, header, effect);
    return points;
}

static inline void fl_ir_arc(FlArena* arena, f32 cx, f32 cy, f32 radius, f32 thickness, f32 start_angle, f32 sweep,
                             FlColor color, u32 flags, const FlEffect* effect) {
    u32 content = (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdArc)) + fl_ir_effect_bytes(effect);
    FlCmdHeader* header = fl_ir_header(arena, FlCmd_Arc, content);
    FlCmdArc* cmd = arena_alloc_zero(arena, FlCmdArc);
    cmd->cx = cx;
    cmd->cy = cy;
    cmd->radius = radius;
    cmd->thickness = thickness;
    cmd->start_angle = start_angle;
    cmd->sweep = sweep;
    cmd->color = color;
    cmd->flags = flags;
    fl_ir_append_effect(arena, header, effect);
}

static inline void fl_ir_circle_stroked(FlArena* arena, f32 cx, f32 cy, f32 radius, FlColor color, FlColor border_color,
                                        f32 border_width, FlStrokeStyle stroke, const FlEffect* effect) {
    u32 content = (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdCircle)) + fl_ir_effect_bytes(effect);
    FlCmdHeader* header = fl_ir_header(arena, FlCmd_Circle, content);
    FlCmdCircle* cmd = arena_alloc_zero(arena, FlCmdCircle);
    cmd->cx = cx;
    cmd->cy = cy;
    cmd->radius = radius;
    cmd->color = color;
    cmd->border_color = border_color;
    cmd->border_width = border_width;
    cmd->stroke = stroke;
    fl_ir_append_effect(arena, header, effect);
}

static inline void fl_ir_circle(FlArena* arena, f32 cx, f32 cy, f32 radius, FlColor color, FlColor border_color,
                                f32 border_width, const FlEffect* effect) {
    fl_ir_circle_stroked(arena, cx, cy, radius, color, border_color, border_width, (FlStrokeStyle) { 0 }, effect);
}

static inline void fl_ir_diamond(FlArena* arena, f32 cx, f32 cy, f32 rx, f32 ry, FlColor color, FlColor border_color,
                                 f32 border_width, const FlEffect* effect) {
    u32 content = (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdDiamond)) + fl_ir_effect_bytes(effect);
    FlCmdHeader* header = fl_ir_header(arena, FlCmd_Diamond, content);
    FlCmdDiamond* cmd = arena_alloc_zero(arena, FlCmdDiamond);
    cmd->cx = cx;
    cmd->cy = cy;
    cmd->rx = rx;
    cmd->ry = ry;
    cmd->color = color;
    cmd->border_color = border_color;
    cmd->border_width = border_width;
    fl_ir_append_effect(arena, header, effect);
}

static inline FlBar* fl_ir_bars(FlArena* arena, f32 baseline_y, FlColor color, u32 bar_count, const FlEffect* effect) {
    u32 content = (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdBarSet)) + bar_count * (u32)sizeof(FlBar)
                  + fl_ir_effect_bytes(effect);
    FlCmdHeader* header = fl_ir_header(arena, FlCmd_Bar, content);
    FlCmdBarSet* cmd = arena_alloc_zero(arena, FlCmdBarSet);
    cmd->baseline_y = baseline_y;
    cmd->color = color;
    cmd->bar_count = bar_count;
    FlBar* bars = arena_alloc_array(arena, FlBar, bar_count);
    fl_ir_append_effect(arena, header, effect);
    return bars;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lay a UTF-8 string out into a positioned FlCmd_TextRun and append it. Glyph ids are codepoints (the backend
// re-resolves and rasterizes each), resolved here only to advance the pen. cursor_x is the pen x of the first
// glyph; baseline_y is the text baseline. The transient FlGlyph array is built in scratch, which MUST be a
// different arena than arena (the latter holds the contiguous IR byte stream).

// size_q4 is the raster (preparation/atlas) size as a quarter-px key (font_cache_whole_px_to_q4 /
// font_cache_quantize_size_q4); scale scales the run to its display size (see FlCmdTextRun). Advances are
// taken at size_q4 and scaled by scale so the pen tracks the display size, matching the scaled glyph
// quads the backend emits. Pass scale == 1.0f for 1:1 (no zoom) text. letter_spacing adds extra px
// between glyphs only (n−1 gaps, no trailing gap) - the same rule font_cache_measure_text applies, so a
// measured run matches the drawn one.
static inline void fl_ir_text_layout(FlArena* arena, FlArena* scratch, FlFontCache* font_cache, FlFontId font,
                                     u16 size_q4, f32 scale, FlColor color, FlString text, float cursor_x,
                                     float baseline_y, float letter_spacing) {
    FlGlyph* glyphs = arena_pos_type(scratch, FlGlyph);
    u32 count = 0;

    Utf8Iter it = utf8_iter(text);
    while (!utf8_iter_done(&it)) {
        u32 codepoint = utf8_iter_next(&it);
        if (codepoint == 0) {
            break;
        }
        if (count > 0) {
            cursor_x += scale * letter_spacing;
        }
        FlGlyph* eg = arena_alloc(scratch, FlGlyph);
        eg->glyph_id = codepoint;
        eg->x = cursor_x;
        eg->y = baseline_y;
        count++;
        cursor_x += scale * (float)font_cache_get_glyph_advance_q4(font_cache, font, size_q4, codepoint) / 64.0f;
    }

    fl_ir_text_run(arena, font, size_q4, scale, color, glyphs, count);
}
