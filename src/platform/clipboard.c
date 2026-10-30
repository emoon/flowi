#include "clipboard.h"

// Weak no-op fallback for the SDL-free platform lib; the strong SDL definition in
// platform_sdl overrides these when it is linked.

__attribute__((weak)) FlString backend_get_clipboard_text(struct FlArena* arena) {
    (void)arena;
    return string_empty();
}

__attribute__((weak)) bool backend_set_clipboard_text(FlString text) {
    (void)text;
    return false;
}
