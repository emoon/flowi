#pragma once

#include <core/types.h>
#include "font_cache.h"

struct FlArena;

// True if `text` holds a tab byte.
bool shaped_run_text_has_tab(FlString text);

// Where tabs land and what draws in their place.
typedef struct TabLayout {
    i64 stop;            // the tab stop, in 26.6 pixels
    FlShapedGlyph blank; // the glyph drawn for a tab
} TabLayout;

// Copy `shaped` into `arena` with every tab moved to the next multiple of `tabs.stop` (counted from the pen origin of
// the run) and the glyphs after it moved with it. A tab glyph is drawn as `tabs.blank`, so it stays in the glyph list
// but leaves no mark. The width grows by the same shift. `text` is the string `shaped` was shaped from.
FlShapedRun shaped_run_apply_tabs(struct FlArena* arena, FlString text, FlShapedRun shaped, TabLayout tabs);
