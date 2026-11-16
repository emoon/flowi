#pragma once

#include <ui/render_ir.h>
#include <stdbool.h>

// View: pan (pixels) + zoom, mapping authoring/world coordinates to screen as screen = world * zoom + pan. The
// transform is applied to every coordinate and size as the scene is built. Angles are scale-invariant and
// pass through untouched; the procedural image's source sub-rect is in texel space and is never transformed.

typedef struct View {
    f32 zoom;
    f32 pan_x;
    f32 pan_y;
    f32 screen_w; // window size in pixels
    f32 screen_h;
} View;

// World-space rectangle the window currently shows: the inverse of the view transform applied to the full
// window rect (screen = world*zoom + pan  =>  world = (screen - pan)/zoom).
static inline FlRect view_world_viewport(View v) {
    return (FlRect) { -v.pan_x / v.zoom, -v.pan_y / v.zoom, v.screen_w / v.zoom, v.screen_h / v.zoom };
}

// True when a world-space AABB is at least partly on screen. margin_world pads the test so a primitive whose
// origin is just off screen but whose body reaches in is kept.
static inline bool view_visible(View v, FlRect world, f32 margin_world) {
    FlRect vp = view_world_viewport(v);
    return !(world.x + world.w < vp.x - margin_world || world.x > vp.x + vp.w + margin_world
             || world.y + world.h < vp.y - margin_world || world.y > vp.y + vp.h + margin_world);
}

static inline f32 v_x(View v, f32 x) {
    return x * v.zoom + v.pan_x;
}
static inline f32 v_y(View v, f32 y) {
    return y * v.zoom + v.pan_y;
}
static inline f32 v_len(View v, f32 l) {
    return l * v.zoom;
}
static inline FlRect v_rect(View v, FlRect r) {
    return (FlRect) { r.x * v.zoom + v.pan_x, r.y * v.zoom + v.pan_y, r.w * v.zoom, r.h * v.zoom };
}
static inline FlCornerRadius v_radius(View v, FlCornerRadius c) {
    return (FlCornerRadius) { c.tl * v.zoom, c.tr * v.zoom, c.bl * v.zoom, c.br * v.zoom };
}
