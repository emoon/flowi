#include "text.h"
#include <stdio.h>
#include "font_private.h"
#include "render.h"

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
/*
const u8* utf8_simple(const u8* s, u32* c) {
    const u8* next = NULL;

    if (s[0] < 0x80) {
        *c = s[0];
        next = s + 1;
    } else if ((s[0] & 0xe0) == 0xc0) {
        *c = ((long)(s[0] & 0x1f) << 6) | ((long)(s[1] & 0x3f) << 0);
        next = s + 2;
    } else if ((s[0] & 0xf0) == 0xe0) {
        *c = ((long)(s[0] & 0x0f) << 12) | ((long)(s[1] & 0x3f) << 6) | ((long)(s[2] & 0x3f) << 0);
        next = s + 3;
    } else if ((s[0] & 0xf8) == 0xf0 && (s[0] <= 0xf4)) {
        *c = ((long)(s[0] & 0x07) << 18) | ((long)(s[1] & 0x3f) << 12) | ((long)(s[2] & 0x3f) << 6) |
             ((long)(s[3] & 0x3f) << 0);
        next = s + 4;
    } else {
        *c = -1;       // invalid
        next = s + 1;  // skip this byte
    }
    if (*c >= 0xd800 && *c <= 0xdfff)
        *c = -1;  // surrogate half
    return next;
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convert utf8 to codepoints (u32) Will return false if the input utf8 is is invalid
// Output is to be expected to be 16 byte aligned and contain 32 bytes of extra data
bool utf8_to_codepoints_u32(u32* output, const u8* input, int len) {
    /*
    const u8* end = input + len;
    while (input < end) {
        input = utf8_simple(input, output);
        output++;
    }
    */

    for (int i = 0; i < len; ++i) {
        *output++ = *input++;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Text_generate_vertex_buffer_ref(FlVertPosUvColor* FL_RESTRICT out, FlIdxSize* FL_RESTRICT index_buffer,
                                     const Font* FL_RESTRICT font, u32 font_size, const u32* FL_RESTRICT codepoints,
                                     u32 color, FlVec2 pos, FlIdxSize vertex_id, int count) {
    for (int i = 0; i < count; ++i) {
        u32 cp = *codepoints++;

        Glyph* g = Font_get_glyph(font, cp, font_size);

        // TODO: Should never happen, should log error here
        if (!g) {
            continue;
        }

        u16 x0 = g->x0;
        u16 y0 = g->y0;
        u16 x1 = g->x1;
        u16 y1 = g->y1;

        float rx = g->x_offset + pos.x;
        float ry = g->y_offset + pos.y;

        float nx0 = rx;
        float ny0 = ry;
        float nx1 = rx + (x1 - x0);
        float ny1 = ry + (y1 - y0);

        out[0].x = nx0;
        out[0].y = ny0;
        out[0].u = x0;
        out[0].v = y0;
        out[0].color = color;

        out[1].x = nx1;
        out[1].y = ny0;
        out[1].u = x1;
        out[1].v = y0;
        out[1].color = color;

        out[2].x = nx1;
        out[2].y = ny1;
        out[2].u = x1;
        out[2].v = y1;
        out[2].color = color;

        out[3].x = nx0;
        out[3].y = ny1;
        out[3].u = x0;
        out[3].v = y1;
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
// Calculate AABB for the text
// TODO: Supply styling for text spacing and stuff like that

FlVec2 Text_calculate_size(const struct Glyph* FL_RESTRICT glyph_lookup, const u32* FL_RESTRICT codepoints, int count) {
    FlVec2 size = {0.0f, 0.0f};

    // TODO: Separate array for lookup_x for better cache locality?
    // TODO: Support left -> right text

    for (int i = 0; i < count; ++i) {
        const Glyph* g = &glyph_lookup[*codepoints++];
        u16 y_size = g->y1 - g->y0;
        size.x += g->advance_x;
        size.y = y_size > size.y ? y_size : size.y;
    }

    return size;
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Based on 
// Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
// but slightly rewritten to allow it to be fully branchless

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

// clang-format off
static const uint8_t utf8d[] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
  8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
  0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
  0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
  0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
  1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
  1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
  1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
};
// clang-format on

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_INLINE u32 decode(u32 state, u32* codep, u32 byte) {
    u32 type = utf8d[byte];
    u32 t = *codep;

    const u32 t0 = (byte & 0x3fu) | (t << 6);
    const u32 t1 = (0xff >> type) & (byte);

    t = (state != UTF8_ACCEPT) ? t0 : t1;
    *codep = t;

    state = utf8d[256 + state * 16 + type];
    return state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Converts utf8 string to codepoints

Utf8Result Utf8_to_codepoints_u32(LinearAllocator* allocator, const uint8_t* input, int len) {
    Utf8Result res = {0};
    u32 state = 0;

    u32* output = LinearAllocator_alloc_array(allocator, u32, len * 4);
    u32* temp = output;

    for (int i = 0; i < len; ++i) {
        state = decode(state, output, input[i]);
        output = state == UTF8_ACCEPT ? output + 1 : output;
    }

    if (state != UTF8_ACCEPT) {
        res.error = FlError_Utf8Malformed;
    } else {
        res.codepoints = temp;
        res.len = (output - temp);
    }

    return res;
}
