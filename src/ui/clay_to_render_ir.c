///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core: Clay render output -> neutral render-command IR.
//
// See clay_to_render_ir.h. The translation stops at intent: the backend (render_ir.h consumer) is responsible
// for premultiplying, folding global_alpha, clamping/splitting corners, and resolving fonts/images.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "clay_to_render_ir.h"

#include <core/arena.h>
#include <core/perf_scope.h>
#include "external/clay/clay.h"
#include "font_cache.h"
#include "render_ir.h"
#include "render_ir_build.h"

#include <math.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clay value types carry channels as floats (0-255 by convention); the IR carries u8. Truncated with no
// premultiply or alpha fold.

static inline FlColor to_em_color(Clay_Color c) {
    return (FlColor) { (u8)c.r, (u8)c.g, (u8)c.b, (u8)c.a };
}

static inline FlRect to_em_rect(Clay_BoundingBox bb) {
    return (FlRect) { bb.x, bb.y, bb.width, bb.height };
}

static inline FlCornerRadius to_em_radius(Clay_CornerRadius r) {
    return (FlCornerRadius) { r.topLeft, r.topRight, r.bottomLeft, r.bottomRight };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void clay_rectangle_to_ir(FlArena* arena, const Clay_RenderCommand* cmd) {
    const Clay_RectangleRenderData* rect = &cmd->renderData.rectangle;
    FlColor c = to_em_color(rect->backgroundColor);
    FlColor corners[4] = { c, c, c, c }; // solid fill = four equal corner colors
    fl_ir_fill_rect(arena, to_em_rect(cmd->boundingBox), to_em_radius(rect->cornerRadius), corners, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void clay_border_to_ir(FlArena* arena, const Clay_RenderCommand* cmd) {
    const Clay_BorderRenderData* border = &cmd->renderData.border;
    fl_ir_border_stroked(arena, to_em_rect(cmd->boundingBox), (f32)border->width.left, (f32)border->width.right,
                         (f32)border->width.top, (f32)border->width.bottom, to_em_radius(border->cornerRadius),
                         to_em_color(border->color), (FlStrokeStyle) { border->dashLen, border->gapLen });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void clay_image_to_ir(FlArena* arena, const Clay_RenderCommand* cmd) {
    const Clay_ImageRenderData* image = &cmd->renderData.image;
    // ui_image_impl / ui_dynamic_image_impl stash an FlImageSource descriptor in Clay's imageData slot.
    const FlImageSource* source = (const FlImageSource*)image->imageData;
    if (source == nullptr) {
        return;
    }
    // FlCmdImage carries no corner radius, so a rounded Clay image loses its rounding through the IR.
    fl_ir_image(arena, *source, FlImageMode_Color, to_em_rect(cmd->boundingBox), (FlRect) { 0 },
                to_em_color(image->backgroundColor), FlImageFilter_Linear, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Splice one painter span. The painter records each element's primitives in element-local pixel
// space (origin 0,0) into a span referenced by the element's Clay CUSTOM command. Each span record is copied into
// the frame IR verbatim, its absolute position fields translated by the element's resolved rect origin
// (dx, dy). Widths, radii, sweeps, intensities, font sizes, and effect offsets are deltas/extents and are not
// translated.

static void splice_paint_record(FlArena* arena, const FlCmdHeader* rec, float dx, float dy) {
    fl_arena_align(arena, 8); // records start 8-aligned; byte_size is a multiple of 8 so the walk stays valid
    FlCmdHeader* dst = (FlCmdHeader*)arena_alloc_array(arena, u8, rec->byte_size);
    memcpy(dst, rec, rec->byte_size);

    void* payload = dst + 1;
    switch ((FlCmdKind)dst->kind) {
        case FlCmd_FillRect: {
            FlCmdFillRect* c = (FlCmdFillRect*)payload;
            c->rect.x += dx;
            c->rect.y += dy;
            if (dst->flags & FL_SNAP_DEVICE_PX) {
                // Snap to the device-pixel grid in absolute space - the element origin (carrying a fractional
                // scroll offset or fractional DPI) is folded in.
                c->rect.x = roundf(c->rect.x);
                c->rect.y = roundf(c->rect.y);
                c->rect.w = roundf(c->rect.w);
                c->rect.h = roundf(c->rect.h);
            }
            break;
        }
        case FlCmd_Border: {
            FlCmdBorder* c = (FlCmdBorder*)payload;
            c->rect.x += dx;
            c->rect.y += dy;
            break;
        }
        case FlCmd_TextRun: {
            FlCmdTextRun* c = (FlCmdTextRun*)payload;
            FlGlyph* glyphs = (FlGlyph*)(c + 1);
            for (u32 i = 0; i < c->glyph_count; i++) {
                glyphs[i].x += dx;
                glyphs[i].y += dy;
            }
            break;
        }
        case FlCmd_Image: {
            FlCmdImage* c = (FlCmdImage*)payload;
            c->dst.x += dx;
            c->dst.y += dy;
            break;
        }
        case FlCmd_ClipPush: {
            FlCmdClipPush* c = (FlCmdClipPush*)payload;
            c->rect.x += dx;
            c->rect.y += dy;
            break;
        }
        case FlCmd_ClipPop:
            break;
        case FlCmd_Polyline: {
            FlCmdPolyline* c = (FlCmdPolyline*)payload;
            c->baseline_y += dy;
            FlPolyPoint* pts = (FlPolyPoint*)(c + 1);
            for (u32 i = 0; i < c->point_count; i++) {
                pts[i].x += dx;
                pts[i].y += dy;
            }
            break;
        }
        case FlCmd_Arc: {
            FlCmdArc* c = (FlCmdArc*)payload;
            c->cx += dx;
            c->cy += dy;
            break;
        }
        case FlCmd_Circle: {
            FlCmdCircle* c = (FlCmdCircle*)payload;
            c->cx += dx;
            c->cy += dy;
            break;
        }
        case FlCmd_Diamond: {
            FlCmdDiamond* c = (FlCmdDiamond*)payload;
            c->cx += dx;
            c->cy += dy;
            break;
        }
        case FlCmd_Bar: {
            FlCmdBarSet* c = (FlCmdBarSet*)payload;
            c->baseline_y += dy;
            FlBar* bars = (FlBar*)(c + 1);
            for (u32 i = 0; i < c->bar_count; i++) {
                bars[i].x += dx;
            }
            break;
        }
    }
}

static void clay_custom_to_ir(FlArena* arena, const Clay_RenderCommand* cmd) {
    const FlPaintSpan* span = (const FlPaintSpan*)cmd->renderData.custom.customData;
    if (span == nullptr) {
        return;
    }
    const float dx = cmd->boundingBox.x;
    const float dy = cmd->boundingBox.y;
    for (const FlPaintSegment* seg = span->first; seg != nullptr; seg = seg->next) {
        const u8* p = (const u8*)seg->ir;
        const u8* end = p + seg->size;
        while (p < end) {
            const FlCmdHeader* rec = (const FlCmdHeader*)p;
            splice_paint_record(arena, rec, dx, dy);
            p += rec->byte_size;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void clay_text_to_ir(FlArena* arena, FlArena* scratch, FlFontCache* font_cache, const Clay_RenderCommand* cmd) {
    const Clay_TextRenderData* text_data = &cmd->renderData.text;
    const Clay_BoundingBox bb = cmd->boundingBox;

    const char* text = text_data->stringContents.chars;
    int length = text_data->stringContents.length;
    if (length == 0 || text == nullptr) {
        return;
    }

    // Glyph list provides baseline metrics (ascender/descender).
    GlyphList glyph_list = font_cache_get_glyph_list(font_cache, text_data->fontId, text_data->fontSize);
    if (glyph_list.count == 0) {
        return;
    }

    float ascender = (float)glyph_list.ascender / 64.0f;
    float descender = (float)glyph_list.descender / 64.0f;
    float baseline_y;
    switch (text_data->verticalAlignment) {
        case CLAY_TEXT_VALIGN_TOP:
            baseline_y = bb.y + ascender;
            break;
        case CLAY_TEXT_VALIGN_CENTER:
            baseline_y = bb.y + bb.height / 2.0f + (ascender + descender) / 2.0f;
            break;
        case CLAY_TEXT_VALIGN_BOTTOM:
            baseline_y = bb.y + bb.height + descender;
            break;
        case CLAY_TEXT_VALIGN_BASELINE:
        default:
            baseline_y = bb.y + bb.height;
            break;
    }

    // 1:1 scale: layout already sizes glyphs at the resolved font size; there is no separate display zoom here.
    // Letter spacing is applied between glyphs (n−1 gaps), the same rule font_cache_measure_text and the painter
    // follow.
    fl_ir_text_layout(arena, scratch, font_cache, text_data->fontId, font_cache_whole_px_to_q4(text_data->fontSize),
                      1.0f, to_em_color(text_data->textColor), (FlString) { .data = text, .length = length }, bb.x,
                      baseline_y, (float)text_data->letterSpacing);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_clay_to_render_ir(FlArena* arena, FlArena* scratch, FlFontCache* font_cache, Clay_RenderCommand* clay_commands,
                          int clay_count) {
    perf_scope_function();

    for_count(i, clay_count) {
        Clay_RenderCommand* cmd = &clay_commands[i];
        switch (cmd->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
                clay_rectangle_to_ir(arena, cmd);
                break;
            case CLAY_RENDER_COMMAND_TYPE_IMAGE:
                clay_image_to_ir(arena, cmd);
                break;
            case CLAY_RENDER_COMMAND_TYPE_TEXT:
                clay_text_to_ir(arena, scratch, font_cache, cmd);
                break;
            case CLAY_RENDER_COMMAND_TYPE_BORDER:
                clay_border_to_ir(arena, cmd);
                break;
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
                fl_ir_clip_push(arena, to_em_rect(cmd->boundingBox));
                break;
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
                fl_ir_clip_pop(arena);
                break;
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
                clay_custom_to_ir(arena, cmd);
                break;
            default:
                break;
        }
    }
}
