#pragma once

#include "view.h"
#include <SDL2/SDL.h>

typedef struct InputOutcome {
    bool quit;
    bool redraw;
    bool resized; // the view's screen size changed; the backend surface must follow
} InputOutcome;

// Applies one SDL event to the view and reports what the host has to do about it.
InputOutcome input_apply(View* view, const SDL_Event* event);
