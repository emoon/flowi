// Public UI feedback-event seam - flowi reports what happened in the interaction layer (focus moved between
// widgets, a directional nav hit an edge, a widget was activated) through one app-installed hook, and the app
// maps each event kind to whatever it wants.
//
// The hook is NULL by default and fires only on discrete, human-driven interactions (never per frame).

#pragma once

#include <flowi/ui/element_id.h>

// What happened. The enum is open to growth without breaking the seam - apps filter by kind.
typedef enum FlUiFeedbackKind {
    // Focus moved from one widget to another (arrow/D-pad spatial nav, or Tab order). from/to/dir set.
    FlUiFeedbackKind_NavMoved = 0,
    // A directional nav was pressed but no candidate existed - an edge was hit, focus stayed put. from/dir set.
    FlUiFeedbackKind_NavBlocked = 1,
    // The focused widget was activated (Enter/Space/Gamepad-A). to is the activated widget.
    FlUiFeedbackKind_Activated = 2,
} FlUiFeedbackKind;

// Direction of a NavMoved/NavBlocked event. None for Tab-order moves and Activated (no spatial direction).
typedef enum FlUiFeedbackDir {
    FlUiFeedbackDir_None = 0,
    FlUiFeedbackDir_Up = 1,
    FlUiFeedbackDir_Down = 2,
    FlUiFeedbackDir_Left = 3,
    FlUiFeedbackDir_Right = 4,
} FlUiFeedbackDir;

// One semantic UI feedback event. Borrowed for the duration of the callback only - copy out anything you keep.
typedef struct FlUiFeedbackEvent {
    FlUiFeedbackKind kind;
    FlElementId from;    // NavMoved/NavBlocked: previously focused widget (0 if none). Unused otherwise.
    FlElementId to;      // NavMoved: newly focused widget. Activated: the activated widget. Unused otherwise.
    FlUiFeedbackDir dir; // NavMoved/NavBlocked: direction pressed. None otherwise.
} FlUiFeedbackEvent;

// App-installed feedback hook. event is valid only for the call. user_data is whatever was passed to
// fl_ui_set_feedback_hook.
typedef void (*FlUiFeedbackCallback)(const FlUiFeedbackEvent* event, void* user_data);

// Install (or clear, with cb == NULL) the app's UI feedback hook. App-wide: one hook shared across all windows.
// NULL by default; no event is emitted until a hook is installed.
void fl_ui_set_feedback_hook(FlUiFeedbackCallback cb, void* user_data);
