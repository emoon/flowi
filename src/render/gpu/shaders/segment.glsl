// Distance from point p to the segment a..b (the capsule centerline; the clamped projection gives round
// caps). Shared by classification's line-aware narrow phase and the raster pass's polyline coverage.
float segment_dist(float2 p, float2 a, float2 b) {
    float2 pa = p - a;
    float2 ba = b - a;
    float bb = dot(ba, ba);
    float h = bb > 0.0 ? clamp(dot(pa, ba) / bb, 0.0, 1.0) : 0.0;
    return length(pa - ba * h);
}
