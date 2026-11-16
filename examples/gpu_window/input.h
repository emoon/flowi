#pragma once

#include "view.h"
#include <SDL2/SDL.h>

typedef struct InputOutcome {
    bool quit;
    bool redraw;
    bool resized;
} InputOutcome;

InputOutcome input_apply(View* view, const SDL_Event* event);
