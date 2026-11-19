#include <core/math.h>
#include <core/simd.h>
#include "software_renderer_internal.h"
#include "sr_tile_row.h"
#include <math.h>
#include <stddef.h>

typedef struct TextMode {
    bool lcd;
    bool use_write_mask;
} TextMode;

typedef struct GlyphRow {
    const u8* r;
    const u8* g;
    const u8* b;
} GlyphRow;

typedef struct TextPaint {
    SoaRgba color;
    i16x8 write_mask;
    i16x8 coverage_scale;
} TextPaint;

typedef struct TextJob {
    TileTarget target;
    const RenderText* text;
    TileClip clip;
    TextPaint paint;
} TextJob;

static ALWAYS_INLINE i16x8 load_coverage(const u8* glyph_line, i16x8 coverage_scale) {
    i16x8 alpha = u8x8_load_to_i16x8_wide_shl(glyph_line, 0, 7);
    return i16x8_lerp(i16x8_new_splat(0), alpha, coverage_scale);
}

ALWAYS_INLINE static void draw_text_line(const TextMode mode, const PlaneRow* out, const GlyphRow* glyph,
                                         const TextPaint* paint) {
    i16x8 alpha_r_wide = load_coverage(glyph->r, paint->coverage_scale);
    i16x8 alpha_g_wide = alpha_r_wide;
    i16x8 alpha_b_wide = alpha_r_wide;

    if (mode.lcd) {
        alpha_g_wide = load_coverage(glyph->g, paint->coverage_scale);
        alpha_b_wide = load_coverage(glyph->b, paint->coverage_scale);
    }

    const SoaRgb bg = plane_load_rgb(out);

    SoaRgb result = {
        .r = i16x8_lerp(bg.r, paint->color.r, alpha_r_wide),
        .g = i16x8_lerp(bg.g, paint->color.g, alpha_g_wide),
        .b = i16x8_lerp(bg.b, paint->color.b, alpha_b_wide),
    };

    if (mode.use_write_mask) {
        result.r = i16x8_select(paint->write_mask, result.r, bg.r);
        result.g = i16x8_select(paint->write_mask, result.g, bg.g);
        result.b = i16x8_select(paint->write_mask, result.b, bg.b);
    }

    store_rgb(result, out);
}

static ALWAYS_INLINE TextPaint text_paint_new(const RenderText* text, int xlen) {
    TextPaint paint;
    paint.color = splat_color(text->color);
    paint.write_mask = i16x8_write_mask(xlen);
    // The frame's global_alpha is already folded into the text color's alpha when the command is emitted, so
    // the color's alpha (Q7) is the coverage scale.
    paint.coverage_scale = i16x8_new_splat(i16x8_extract(paint.color.a, 0));
    return paint;
}

static ALWAYS_INLINE GlyphRow glyph_row_new(const TextJob* job, const bool lcd, int y) {
    const RenderText* text = job->text;
    const int glyph_x = i32x4_extract(job->clip.clip_diff, 0);
    const int glyph_y = i32x4_extract(job->clip.clip_diff, 1);
    const int glyph_offset = (glyph_y + y) * text->stride + glyph_x;
    return (GlyphRow) {
        .r = text->data_r + glyph_offset,
        .g = lcd ? text->data_g + glyph_offset : nullptr,
        .b = lcd ? text->data_b + glyph_offset : nullptr,
    };
}

static ALWAYS_INLINE void draw_text_row(const bool lcd, const TextJob* job, int y) {
    GlyphRow glyph = glyph_row_new(job, lcd, y);
    PlaneRow out = plane_row_new(job->target.output + y * TILE_STRIDE, job->clip.write_offset);

    for_count(x, (job->clip.xlen >> 3)) {
        draw_text_line((TextMode) { .lcd = lcd, .use_write_mask = false }, &out, &glyph, &job->paint);
        glyph.r += 8;
        glyph.g = lcd ? glyph.g + 8 : nullptr;
        glyph.b = lcd ? glyph.b + 8 : nullptr;
        out.line_output += 8;
    }

    if ((job->clip.xlen & 0x7) > 0) {
        draw_text_line((TextMode) { .lcd = lcd, .use_write_mask = true }, &out, &glyph, &job->paint);
    }
}

static ALWAYS_INLINE bool text_job_prepare(TextJob* job) {
    if (!tile_clip_setup(job->text->aabb, &job->target, &job->clip)) {
        return false;
    }

    job->paint = text_paint_new(job->text, job->clip.xlen);
    return true;
}

static void sr_render_text_lcd(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect,
                               const TileInfo* info) {
    TextJob job = { .target = { tile_output, scissor_rect, info }, .text = &command->render_text };

    if (text_job_prepare(&job)) {
        for_count(y, job.clip.ylen) {
            draw_text_row(true, &job, y);
        }
    }
}

static void sr_render_text_grayscale(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect,
                                     const TileInfo* info) {
    TextJob job = { .target = { tile_output, scissor_rect, info }, .text = &command->render_text };

    if (text_job_prepare(&job)) {
        for_count(y, job.clip.ylen) {
            draw_text_row(false, &job, y);
        }
    }
}

void sr_render_text(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* info) {
    if (command->render_text.data_g == nullptr) {
        sr_render_text_grayscale(tile_output, command, scissor_rect, info);
    } else {
        sr_render_text_lcd(tile_output, command, scissor_rect, info);
    }
}
