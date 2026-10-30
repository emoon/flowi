#pragma once
// Clipboard seam: a weak no-op definition (clipboard.c) overridden by the strong SDL definition when linked.

#include <core/string.h>

struct FlArena;

FlString backend_get_clipboard_text(struct FlArena* arena);
bool backend_set_clipboard_text(FlString text);
