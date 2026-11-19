#include "sr_render_helpers.h"

#include <core/arena.h>
#include "sr_command.h"
#include "sw_glyph_cache.h"

static bool glyph_is_empty(const Glyph* glyph) {
    return glyph == nullptr || (glyph->advance_x == 0 && glyph->width == 0);
}

typedef struct GlyphBox {
    float x0;
    float y0;
    float x1;
    float y1;
} GlyphBox;

static bool glyph_is_drawable(const Glyph* glyph, const GlyphBox* box) {
    return box->x1 > box->x0 && box->y1 > box->y0 && glyph->bitmap_data_r != nullptr;
}

float sr_emit_glyph(FlArena* arena, const Glyph* glyph, GlyphPen pen, GlyphStyle style) {
    if (glyph_is_empty(glyph)) {
        return pen.cursor_x;
    }

    GlyphBox box;
    box.x0 = floorf(pen.cursor_x + 0.5f) + ((float)glyph->bearing_x / 64.0f);
    box.y0 = pen.baseline_y - (float)glyph->bearing_y;
    box.x1 = box.x0 + (float)glyph->pixel_width;
    box.y1 = box.y0 + (float)glyph->height;

    if (glyph_is_drawable(glyph, &box)) {
        RenderCommand* cmd = arena_alloc_zero(arena, RenderCommand);
        cmd->mask = RenderMask_DrawQuad | RenderMask_Text;
        cmd->global_alpha = style.global_alpha;
        cmd->render_text.aabb = f32x4_new(box.x0, box.y0, box.x1, box.y1);
        cmd->render_text.color = style.text_color;
        cmd->render_text.data_r = glyph->bitmap_data_r;
        cmd->render_text.data_g = glyph->bitmap_data_g;
        cmd->render_text.data_b = glyph->bitmap_data_b;
        cmd->render_text.stride = glyph->width;
    }

    return pen.cursor_x + (float)glyph->advance_x / 64.0f;
}
