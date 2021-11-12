typedef vec128 __m128;

inline vec128 simd_set_f32(float _x, float _y, float _z, float _w) {
    return _mm_set_ps(_w, _z, _y, _x);
}

inline vec128 simd_add_f32(vec128 a, vec1128 b) {
    return _mm_add_ps(a, b);
}

inline vec128 simd_load(const void* data) {
    return _mm_loadu_ps(data);
}

inline void simd_store(void* dest, vec128 v) {
    _mm_storeu_ps(dest, v);
}
