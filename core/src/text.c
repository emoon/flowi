#include "text.h"
#include "render.h"
#include "font_private.h"

#include <emmintrin.h>  // __m128i
#include <smmintrin.h>
#include <xmmintrin.h>  // __m128

// Convert utf8 to codepoints (u16) Will return false if the input utf8 is is invalid
// Output is to be expected to be 16 byte aligned and contain 32 bytes of extra data
// TODO: Support actual utf8 :)
// TODO: Add utf8 validation pass
// TODO: Currently only supports ascii
// TODO: SIMD
bool Text_utf8_to_codepoints_u16(u16* output, const u8* input, int len) {
    for (int i = 0; i < len; ++i) {
        u8 t = *input++;

        if (t > 0x80) {
            return false;
        }

        *output++ = t;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Text_generate_vertex_buffer_ref(FlVertPosUvColor* __restrict out, FlIdxSize* __restrict index_buffer,
                                     const Glyph* __restrict glyph_lookup, const u32* __restrict codepoints, u32 color,
                                     FlVec2 pos, FlIdxSize vertex_id, int count) {
    for (int i = 0; i < count; ++i) {
        const Glyph* g = &glyph_lookup[*codepoints++];

        out[0].x = pos.x + g->x0;
        out[0].y = pos.y + g->y0;
        out[0].u = g->u0;
        out[0].v = g->v0;
        out[0].color = color;

        out[1].x = pos.x + g->y1;
        out[1].y = pos.y + g->y0;
        out[1].u = g->u1;
        out[1].v = g->v0;
        out[1].color = color;

        out[2].x = pos.x + g->x1;
        out[2].y = pos.y + g->y1;
        out[2].u = g->u1;
        out[2].v = g->v1;
        out[2].color = color;

        out[3].x = pos.x + g->x0;
        out[3].y = pos.y + g->y1;
        out[3].u = g->u0;
        out[3].v = g->v1;
        out[3].color = color;

        index_buffer[0] = vertex_id + 0;
        index_buffer[1] = vertex_id + 1;
        index_buffer[2] = vertex_id + 2;

        index_buffer[3] = vertex_id + 0;
        index_buffer[4] = vertex_id + 2;
        index_buffer[5] = vertex_id + 3;

        pos.x += g->advance_x;

        vertex_id += 4;
        out += 4;
        index_buffer += 6;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define _mm_shuffle_ps_si128(a, b, i) _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(a), _mm_castsi128_ps(b), i))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generate VertexBuffer for text

#if defined(FL_SSE2)

void Text_generate_vertex_buffer_sse2(FlVertPosUvColor* __restrict out, FlIdxSize* __restrict index_buffer,
                                      const Glyph* __restrict glyph_lookup, const u32* __restrict codepoints, u32 color,
                                      FlVec2 pos, FlIdxSize vertex_id, int count) {
#if FL_INDEX_SIZE == 2
    __m128i vertex_id = _mm_set1_epi16(vertex_id);
    __m128i index_add = _mm_set1_epi16(4);
    __m128i index_temp_add = _mm_set_epi16(0, 0, 3, 2, 0, 2, 1, 0);
#else
    __m128i vertex_id = _mm_set1_epi32(vertex_id);
    __m128i index_add = _mm_set1_epi32(4);
    __m128i index_temp_add_0 = _mm_set_epi32(0, 2, 1, 0);
    __m128i index_temp_add_1 = _mm_set_epi32(0, 0, 3, 2);
#endif
    __m128i zero = _mm_setzero_si128();
    __m128i color = _mm_set1_epi32(color_in);
    __m128 current_pos = _mm_set_ps(pos.x, pos.y, pos.x, pos.y);
    __m128i* out = (__m128i*)out_temp;

    for (int i = 0; i < count; ++i) {
        const Glyph* g = &glyph_lookup[*codepoints++];

        __m128i x0y0_x1y1_u0v0_u1v1 = _mm_loadu_si128((__m128i*)g);

        // get x0,y0 - x1,y1 as floats
        __m128i ix0_y0_x1_y1 = _mm_unpackhi_epi16(zero, x0y0_x1y1_u0v0_u1v1);
        __m128 f_x0y0_x1y1 = _mm_cvtepi32_ps(ix0_y0_x1_y1);

        // offset x rectangle to the current position
        f_x0y0_x1y1 = _mm_add_ps(current_pos, f_x0y0_x1y1);

        // combine
        __m128i x0y0_x1y1 = _mm_castps_si128(f_x0y0_x1y1);
        __m128i u0v0_u1v1_color_color = _mm_unpacklo_epi32(x0y0_x1y1_u0v0_u1v1, color);

        __m128i u0v0_color_u1v1_color = _mm_shuffle_epi32(u0v0_u1v1_color_color, 152);    // [0,2,1,2]
        __m128i u0v1_color_u1v0_color = _mm_shufflelo_epi16(u0v0_u1v1_color_color, 108);  // [0,3,2,1,4,5,6,7]

        // clang-format off
        __m128i x0_y0_u0v0_color = _mm_unpacklo_epi64(x0y0_x1y1, u0v0_color_u1v1_color);
        __m128i x1_y0_u1v0_color = _mm_shuffle_ps_si128(x0y0_x1y1, u0v0_color_u1v1_color, 150);  // x0y0_x1y1[2,1],u0v0_color_u1v1_color[1,2]
        __m128i x1_y1_u1v1_color = _mm_unpackhi_epi64(x0y0_x1y1, u0v0_color_u1v1_color);
        __m128i x0_y1_u0v1_color = _mm_shuffle_ps_si128(x0y0_x1y1, u0v1_color_u1v0_color, 140);  // x0y0_x1y1[0,3],u0v1_color_u1v0_color[0,2]
        // clang-format on

        out[0] = x0_y0_u0v0_color;
        out[1] = x1_y0_u1v0_color;
        out[2] = x1_y1_u1v1_color;
        out[3] = x0_y1_u0v1_color;

        // bump index count

#if FL_INDEX_SIZE == 2
        __m128i t = _mm_add_epi16(vertex_id, index_temp_add);
        _mm_storeu_si128((__m128i*)index_buffer, t);
        vertex_id = _mm_add_epi16(vertex_id, index_add);
#else
        __m128i t0 = _mm_add_epi32(vertex_id, index_temp_add_0);
        __m128i t1 = _mm_add_epi32(vertex_id, index_temp_add_1);
        _mm_storeu_si128((__m128i*)index_buffer, t);
        _mm_storeu_si128((__m128i*)&index_buffer[4], t);
        vertex_id = _mm_add_epi32(vertex_id, index_add);
#endif

        //__m128 advance_x_ss = _mm_loadu_ps(&g->advance_x);
        //__m128 advance_x = _mm_shuffle_ps(advance_x_ss, _mm_castsi128_ps(zero), 4);  // [0,x,0,x] // todo: fix mas
        advance_x = _mm_set_ps(g->advance_x, 0.0f, g->advance->x, 0.0f);  // TODO: opt
        current_pos = _mm_add_ps(current_pos, advance_x);

        out += 4;
        index_buffer += 6;
    }
}

#endif
