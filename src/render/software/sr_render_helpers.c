///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared render helpers implementation
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "sr_render_helpers.h"

#include <core/arena.h>
#include "sr_command.h"
#include "sw_glyph_cache.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float sr_emit_glyph(FlArena* arena, const Glyph* glyph, float cursor_x, float baseline_y, u8x16 text_color,
                    u8 global_alpha) {
    if (glyph == nullptr || (glyph->advance_x == 0 && glyph->width == 0)) {
        return cursor_x;
    }

    float glyph_x = floorf(cursor_x) + ((float)glyph->bearing_x / 64.0f);
    float glyph_y = baseline_y - (float)glyph->bearing_y;
    float glyph_x1 = glyph_x + (float)glyph->pixel_width;
    float glyph_y1 = glyph_y + (float)glyph->height;

    if (glyph_x1 > glyph_x && glyph_y1 > glyph_y && glyph->bitmap_data_r != nullptr) {
        RenderCommand* cmd = arena_alloc_zero(arena, RenderCommand);
        cmd->mask = RenderMask_DrawQuad | RenderMask_Text;
        cmd->global_alpha = global_alpha;
        cmd->render_text.aabb = f32x4_new(glyph_x, glyph_y, glyph_x1, glyph_y1);
        cmd->render_text.color = text_color;
        cmd->render_text.data_r = glyph->bitmap_data_r;
        cmd->render_text.data_g = glyph->bitmap_data_g;
        cmd->render_text.data_b = glyph->bitmap_data_b;
        cmd->render_text.stride = glyph->width;
    }

    return cursor_x + (float)glyph->advance_x / 64.0f;
}
