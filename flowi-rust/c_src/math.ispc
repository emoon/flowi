#if TARGET_WIDTH == 4
static const varying int swizzle_1 = { 0, 4, 1, 5 };
static const varying int swizzle_2 = { 2, 6, 3, 7 };
#elif TARGET_WIDTH == 8
static const varying int swizzle_1 = { 0, 8, 1, 9, 2, 10, 3, 11 };
static const varying int swizzle_2 = { 4, 12, 5, 13, 6, 14, 7, 15 };
#elif TARGET_WIDTH == 16
static const varying int swizzle_1 = { 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23 };
static const varying int swizzle_2 = { 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31 };
#else
#error
#endif

inline void soa_to_aos2_ispc(uniform float* uniform dst, const varying float src0, const varying float src1) {
    dst[programIndex] = shuffle(src0, src1, swizzle_1);
    dst[programIndex + programCount] = shuffle(src0, src1, swizzle_2);
}


// Calculates sin/cos pairs for a given angle. The step has to be a within [0, 2*PI] 
export void math_sin_cos(uniform float output[], uniform int count, uniform start_angle, uniform float angle_step) {
    const uniform float c0 = 1.0f;
    const uniform float c1 = -4.95348008918096e-1f;
    const uniform float s1 = -1.66521856991541e-1f;
    const uniform float c2 = 3.878259962881e-2f;
    const uniform float s2 = 8.199913018755e-4f;
    const uniform float c3 = -9.24587976263e-4f;
    const uniform float s3 = -1.61475937228e-4f;
    const uniform float pi2 = 3.1415f * 2.0f;
    const uniform float inv_pi2 = (1.0f / pi2);

    uniform int chunks = count & ~(programCount-1);

    for (uniform int i = 0; i < chunks; i += programCount) {
        varying float t = angle_start + (angle_step * (i + programIndex));
        varying float v2 = t * t;
        varying float v3 = v2 * t;
        varying float v4 = v2 * v2;
        varying float cos = (v2 * c1) + c0;
        varying float v5 = v3 * v2;
        varying float sin = (v3 * s1) + t;
        varying float v6 = v3 * v3;
        varying float v7 = v4 * v3;
        cos = (c2 * v4) + cos;
        sin = (s2 * v5) + sin;
        cos = (c3 * v6) + cos;
        sin = (s3 * v7) + sin;

        soa_to_aos2_ispc(&output[i], sin, cos);
    }
}