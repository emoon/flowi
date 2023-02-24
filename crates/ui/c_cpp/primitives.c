#include "primitives.h"
#include "layer.h"

#ifdef TEST

#else

#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Primitive_rect(Layer* layer, FlVec2 pos, FlVec2 size, u32 color) {
    PrimitiveRect* prim = Primitive_alloc_rect(layer);
    prim->pos = pos;
    prim->size = size;
    //prim->border = border;
    prim->color = color;
}
/*
    const float c1 = -4.95348008918096e-1f;
    const float s1 = -1.66521856991541e-1f;
    const float c2 = 3.878259962881e-2f;
    const float s2 = 8.199913018755e-3f;
    const float c3 = -9.24587976263e-4f;
    const float s3 = -1.61475937228e-4f;
*/

/*
void calc_sin_cos(float* output, int count, float a_step) {
    const float c0 = 1.0f;
    const float pi2 = 3.1415f * 2.0f;
    const float inv_pi2 = (1.0f / pi2);

    float a = 0.0f;

    for (int i = 0; i < count; ++i) {
        float t = a;
        float v2 = t * t;
        float v3 = v2 * t;
        float v4 = v2 * v2;
        float cos = (v2 * c1) + c0;
        float v5 = v3 * v2;
        float sin = (v3 * s1) + t;
        float v6 = v3 * v3;
        float v7 = v4 * v3;
        cos = (c2 * v4) + cos;
        sin = (s2 * v5) + sin;
        cos = (c3 * v6) + cos;
        sin = (s3 * v7) + sin;

        *output++ = sin;
        *output++ = cos;

        // next angle
        a += a_step;
    }
}
*/

