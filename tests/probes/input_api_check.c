///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compile-only parity guard: the api_gen-generated public input surface (<flowi/platform/input.h>).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// <flowi/platform/input.h> names FlString (FlTextInputEvent.text) without including it, so define it first.
#include <core/string.h>
#include <flowi/platform/input.h>

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
