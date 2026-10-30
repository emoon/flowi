///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compile-only parity guard: the api_gen-generated public input surface (<flowi/platform/input.h>).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// <flowi/platform/input.h> names FlString (FlTextInputEvent.text) without including it, so define it first.
#include <core/string.h>
#include <flowi/platform/input.h>

// Per-prototype signature identity: a reordered/retyped arg or changed return fails the build.
#define FL_INPUT_FN_MATCHES(fn, ...) \
    _Static_assert(__builtin_types_compatible_p(__typeof__(fn), __VA_ARGS__), #fn " signature drift")

FL_INPUT_FN_MATCHES(fl_platform_input_state_init, void(FlInputState*));
FL_INPUT_FN_MATCHES(fl_platform_input_push_key_event, void(FlUiCtx*, FlKeyEvent));
FL_INPUT_FN_MATCHES(fl_platform_input_push_text_input_event, void(FlUiCtx*, FlTextInputEvent));
FL_INPUT_FN_MATCHES(fl_platform_input_push_mouse_event, void(FlUiCtx*, FlMouseEvent));
FL_INPUT_FN_MATCHES(fl_platform_input_push_gamepad_event, void(FlUiCtx*, FlGamepadEvent));
FL_INPUT_FN_MATCHES(fl_platform_input_push_window_event, void(FlUiCtx*, FlWindowEvent));
FL_INPUT_FN_MATCHES(fl_platform_input_set_mouse_position, void(FlUiCtx*, float, float));
FL_INPUT_FN_MATCHES(fl_platform_input_update, void(FlUiCtx*, double));

#undef FL_INPUT_FN_MATCHES

// Anonymous-union drop-in: every arm is reachable directly off the event, with no intermediate member.
_Static_assert(offsetof(FlMouseEvent, move) == offsetof(FlMouseEvent, button), "mouse arms share the union offset");
_Static_assert(offsetof(FlMouseEvent, move) == offsetof(FlMouseEvent, wheel), "mouse arms share the union offset");
_Static_assert(offsetof(FlMouseEvent, move) == offsetof(FlMouseEvent, relative), "mouse arms share the union offset");
_Static_assert(offsetof(FlGamepadEvent, button) == offsetof(FlGamepadEvent, axis),
               "gamepad arms share the union offset");

static inline float input_api_check_arm_access(const FlMouseEvent* m, const FlGamepadEvent* g, const FlWindowEvent* w) {
    return m->move.x + m->button.x + m->wheel.y + m->relative.delta_x + (float)g->button.button + g->axis.value
           + (float)w->resize.width;
}

// Enumerator override: constants keep their `Fl<Event>_*` spelling though the enum type is `Fl<Event>Type`.
_Static_assert(FlKeyEvent_Repeat == 2, "key event-type discriminants stable");
_Static_assert(FlMouseEvent_Relative_Motion == 6, "mouse event-type discriminants stable");
_Static_assert(FlGamepadEvent_Disconnected == 4, "gamepad event-type discriminants stable");
_Static_assert(FlWindowEvent_Close == 4, "window event-type discriminants stable");
_Static_assert(FlMouseButton_Count == 5, "mouse button discriminants stable");

// Silence the unused-function warning under -Werror: take the address once. (TU links nothing.)
void* const input_api_check_ref = (void*)&input_api_check_arm_access;
