#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared render helpers for software renderer
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <core/simd.h>
#include <core/types.h>
#include <math.h>
#include <ui/image_decode.h>

struct FlArena;
struct Glyph;
struct RenderCommand;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Premultiply RGB by A for each of the 4 pixels packed RGBARGBARGBARGBA in a u8x16.

static inline u8x16 sr_premultiply_rgba(u8x16 rgba) {
    u8 px[16];
    u8x16_store_unaligned(rgba, px, 0);
    for (int i = 0; i < 16; i += 4) {
        for (int c = 0; c < 3; c++) {
            px[i + c] = image_premul_channel(px[i + c], px[i + 3]);
        }
    }
    return u8x16_new(px);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Emit a RenderCommand for a single glyph. Returns the advanced cursor_x.

float sr_emit_glyph(struct FlArena* arena, const struct Glyph* glyph, float cursor_x, float baseline_y,
                    u8x16 text_color, u8 global_alpha);
