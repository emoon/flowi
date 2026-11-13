#pragma once

// Text editing - Internal API
//
// Edit operations on an FlTextInputState. ui_text_edit.c owns the stb_textedit integration and the buffer; the
// widget (ui_text_input.c) drives it through these calls and reads the caret through STB_STATE.

#include <core/math.h>
#include "ui_text_input.h"

typedef struct FlUiCtx FlUiCtx;

// Key codes for ui_text_edit_key. The movement keys can be or'd with STB_TEXTEDIT_K_SHIFT to extend the selection.
#define STB_TEXTEDIT_K_SHIFT 0x400000
#define STB_TEXTEDIT_K_CONTROL 0x200000
#define STB_TEXTEDIT_K_LEFT (0x10000 | 1)
#define STB_TEXTEDIT_K_RIGHT (0x10000 | 2)
#define STB_TEXTEDIT_K_UP (0x10000 | 3)
#define STB_TEXTEDIT_K_DOWN (0x10000 | 4)
#define STB_TEXTEDIT_K_LINESTART (0x10000 | 5)
#define STB_TEXTEDIT_K_LINEEND (0x10000 | 6)
#define STB_TEXTEDIT_K_TEXTSTART (0x10000 | 7)
#define STB_TEXTEDIT_K_TEXTEND (0x10000 | 8)
#define STB_TEXTEDIT_K_DELETE (0x10000 | 9)
#define STB_TEXTEDIT_K_BACKSPACE (0x10000 | 10)
#define STB_TEXTEDIT_K_UNDO (0x10000 | 11)
#define STB_TEXTEDIT_K_REDO (0x10000 | 12)
#define STB_TEXTEDIT_K_WORDLEFT (0x10000 | 13)
#define STB_TEXTEDIT_K_WORDRIGHT (0x10000 | 14)
#define STB_TEXTEDIT_K_PGUP (0x10000 | 15)
#define STB_TEXTEDIT_K_PGDOWN (0x10000 | 16)

#define STB_STATE(state) ((STB_TexteditState*)(state)->stb_state)

// Feed one key to the editor: a STB_TEXTEDIT_K_* code or a character to insert. Restarts the caret blink.
void ui_text_edit_key(FlTextInputState* state, u32 key);

void ui_text_edit_select_all(FlTextInputState* state);

// The selected text, pointing into the buffer; empty when nothing is selected.
FlString ui_text_edit_selection(const FlTextInputState* state);

// Delete the selection. Restarts the caret blink.
void ui_text_edit_cut(FlTextInputState* state);

// Size of text in screen px at the active font and text scale.
Vec2 ui_text_edit_measure(FlUiCtx* ctx, FlString text);
