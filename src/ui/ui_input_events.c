#include "ui_input.h"
#include <platform/input_types.h>
#include <core/string.h>
#include <core/log.h>
#include "ui_private.h"
#include <string.h>

static void key_down(KeyboardState* kb, FlKeyEvent event) {
    if (!kb->keys[event.key]) {
        kb->keys[event.key] = true;
        kb->keys_pressed[event.key] = true;
        kb->key_hold_duration[event.key] = 0.0f;
    }
    kb->modifiers = event.modifiers;
}

static void key_up(KeyboardState* kb, FlKeyEvent event) {
    kb->keys[event.key] = false;
    kb->keys_released[event.key] = true;
    kb->key_hold_duration[event.key] = 0.0f;
    kb->modifiers = event.modifiers;
}

void platform_input_push_key_event(FlUiCtx* ctx, FlKeyEvent event) {
    KeyboardState* kb = &ctx->input_state.keyboard;

    // The unsigned cast also rejects negative key codes.
    if ((u32)event.key >= FlKey_Count) {
        log_debug("Ignoring key event with out-of-range key code %d", (int)event.key);
        return;
    }

    switch (event.type) {
        case FlKeyEvent_Down:
            key_down(kb, event);
            break;

        case FlKeyEvent_Up:
            key_up(kb, event);
            break;

        case FlKeyEvent_Repeat:
            break;
    }
}

// Back off to a codepoint start so a multi-byte character is dropped whole, not split.
static u64 text_input_fit_count(FlString text, u64 room) {
    u64 count = min(text.length, room);
    if (count < text.length) {
        while (count > 0 && (text.data[count] & 0xC0) == 0x80) {
            count--;
        }
    }
    return count;
}

void platform_input_push_text_input_event(FlUiCtx* ctx, FlTextInputEvent event) {
    FlInputState* state = &ctx->input_state;

    // Append each event's text into the frame's buffer, keeping the earliest text on overflow.
    KeyboardState* kb = &state->keyboard;
    u64 used = kb->text_input.length;
    u64 count = text_input_fit_count(event.text, UI_TEXT_INPUT_THIS_FRAME_CAP - used);
    if (count == 0) {
        return;
    }
    memcpy(kb->text_input_storage + used, event.text.data, count);
    kb->text_input = (FlString) { .data = kb->text_input_storage, .length = used + count, .is_static = 0 };
}
