#include "input.h"
#include <math.h>

static InputOutcome handle_key(View* view, SDL_Keycode key) {
    if (key == SDLK_ESCAPE) {
        return (InputOutcome) { .quit = true };
    }
    if (key == SDLK_r) {
        view->pan_x = 0.0f;
        view->pan_y = 0.0f;
        view->zoom = 1.0f;
        return (InputOutcome) { .redraw = true };
    }
    return (InputOutcome) { 0 };
}

static f32 clamp_zoom(f32 zoom) {
    return zoom < 0.1f ? 0.1f : (zoom > 40.0f ? 40.0f : zoom);
}

static void zoom_at_cursor(View* view, const SDL_MouseMotionEvent* motion) {
    f32 z0 = view->zoom;
    f32 z1 = clamp_zoom(z0 * expf(-(f32)motion->yrel * 0.01f));
    f32 cx = (f32)motion->x;
    f32 cy = (f32)motion->y;
    view->pan_x = cx - (cx - view->pan_x) * (z1 / z0);
    view->pan_y = cy - (cy - view->pan_y) * (z1 / z0);
    view->zoom = z1;
}

static InputOutcome handle_motion(View* view, const SDL_MouseMotionEvent* motion) {
    // The motion event carries the held buttons, so a missed button-up cannot leave a drag stuck on.
    if ((motion->state & SDL_BUTTON_LMASK) != 0) {
        // screen = world * zoom + pan, so dragging the cursor shifts the content the same pixels.
        view->pan_x += (f32)motion->xrel;
        view->pan_y += (f32)motion->yrel;
        return (InputOutcome) { .redraw = true };
    }
    if ((motion->state & SDL_BUTTON_RMASK) != 0) {
        // Vertical drag scales zoom and keeps the world point under the cursor fixed.
        zoom_at_cursor(view, motion);
        return (InputOutcome) { .redraw = true };
    }
    return (InputOutcome) { 0 };
}

// Repaint only on a size change or an expose. Other window events (focus, enter/leave, moves)
// do not redraw.
static InputOutcome handle_window(View* view, const SDL_WindowEvent* window) {
    if (window->event == SDL_WINDOWEVENT_SIZE_CHANGED
        && ((f32)window->data1 != view->screen_w || (f32)window->data2 != view->screen_h)) {
        view->screen_w = (f32)window->data1;
        view->screen_h = (f32)window->data2;
        return (InputOutcome) { .redraw = true, .resized = true };
    }
    if (window->event == SDL_WINDOWEVENT_EXPOSED) {
        return (InputOutcome) { .redraw = true };
    }
    return (InputOutcome) { 0 };
}

InputOutcome input_apply(View* view, const SDL_Event* event) {
    switch (event->type) {
        case SDL_QUIT:
            return (InputOutcome) { .quit = true };
        case SDL_KEYDOWN:
            return handle_key(view, event->key.keysym.sym);
        case SDL_MOUSEMOTION:
            return handle_motion(view, &event->motion);
        case SDL_WINDOWEVENT:
            return handle_window(view, &event->window);
        default:
            return (InputOutcome) { 0 };
    }
}
