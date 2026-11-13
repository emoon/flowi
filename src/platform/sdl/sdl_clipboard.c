// SDL clipboard backend.

#include "../clipboard.h"

#if BACKEND_USE_SDL

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

FlString backend_get_clipboard_text(struct FlArena* arena) {
    if (!SDL_HasClipboardText()) {
        return string_empty();
    }

    char* sdl_text = SDL_GetClipboardText();
    if (!sdl_text) {
        return string_empty();
    }

    FlString text = fl_string_copy(arena, string_from_cstr(sdl_text));
    SDL_free(sdl_text);
    return text;
}

bool backend_set_clipboard_text(FlString text) {
    // SDL takes a null-terminated string.
    char* temp = malloc(text.length + 1);
    if (!temp) {
        return false;
    }

    memcpy(temp, text.data, text.length);
    temp[text.length] = '\0';

    int result = SDL_SetClipboardText(temp);
    free(temp);

    return result == 0;
}

#endif // BACKEND_USE_SDL
