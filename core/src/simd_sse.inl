#define vec128 __m128

FL_INLINE vec128 simd_set_f32(float _x, float _y, float _z, float _w) {
    return _mm_set_ps(_w, _z, _y, _x);
}

FL_INLINE vec128 simd_add_f32(vec128 a, vec128 b) {
    return _mm_add_ps(a, b);
}

FL_INLINE vec128 simd_load(const void* data) {
    return _mm_loadu_ps(data);
}

FL_INLINE void simd_store(void* dest, vec128 v) {
    _mm_storeu_ps(dest, v);
}

FL_INLINE vec128 simd_set_i16(u16 v0, u16 v1, u16 v2, u16 v3, u16 v4, u16 v5, u16 v6, u16 v7) {
    return _mm_castsi128_ps(_mm_set_epi16(v0, v1, v2, v3, v4, v5, v6, v7));
}

