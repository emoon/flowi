#include "background.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void background_controller_init(FlBackgroundController* c) {
    *c = (FlBackgroundController) { 0 };
    c->state.current = IMAGE_HANDLE_INVALID;
    c->state.prev = IMAGE_HANDLE_INVALID;
    c->state.alpha = 255;
    c->state.clear_r = 14; // Default theme surface.base color
    c->state.clear_g = 14;
    c->state.clear_b = 16;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void background_controller_set(FlBackgroundController* c, ImageHandle handle) {
    c->state.current = handle;
    c->state.prev = IMAGE_HANDLE_INVALID; // a plain set drops any blend
    c->state.alpha = 255;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void background_controller_set_alpha(FlBackgroundController* c, u8 alpha) {
    c->state.alpha = alpha;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void background_controller_set_clear_color(FlBackgroundController* c, u8 r, u8 g, u8 b) {
    c->state.clear_r = r;
    c->state.clear_g = g;
    c->state.clear_b = b;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void background_controller_set_blend(FlBackgroundController* c, ImageHandle prev, ImageHandle current, u8 t) {
    c->state.prev = prev;
    c->state.current = current;
    c->state.t = t; // 0 = full prev, 255 = full current
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlBackground background_controller_snapshot(const FlBackgroundController* c) {
    const FlBackgroundState* s = &c->state;

    // A valid prev is the sole signal that this frame is a two-image blend; a backend reads it as such.
    return (FlBackground) {
        .current = (FlImageHandle)s->current,
        .prev = (FlImageHandle)s->prev,
        .crossfade_t = (f32)s->t / 255.0f,
        .alpha = s->alpha,
        .clear = { s->clear_r, s->clear_g, s->clear_b, 255 },
    };
}
