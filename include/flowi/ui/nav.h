// Public nav/action query surface: ask what the toolkit's input/focus layer already knows - was an action
// pressed, is a nav direction pending, who has focus - without owning the nav logic. The semantic action
// vocabulary is public; the binding mechanism (which physical input triggers each action) stays internal
// and app-level.
//
// FlString (the type of FlLayoutId.name) is an includer-provided base type, by the same convention the
// generated public headers follow - provide it (e.g. <core/string.h>) before including this header.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <flowi/ui/action.h>
#include <flowi/ui/element_id.h>
#include <flowi/ui/layout.h>

// Snapshot of this frame's navigation inputs (focused id + pending directional nav), used for cross-frame
// tracking and navigation-boundary auto-detection.
typedef struct FlNavFrame {
    uint32_t focused_id;
    bool nav_left;
    bool nav_right;
    bool nav_up;
    bool nav_down;
} FlNavFrame;

// True if action was pressed this frame. Context-free: reads the active UI context's per-frame action state.
bool fl_action_pressed(FlAction action);

// True if navigation up / down was pressed this frame (arrow, D-pad, or left stick).
bool fl_nav_up_pressed(void);
bool fl_nav_down_pressed(void);

// Consume a pending navigation direction so the focus system won't also act on it this frame. Use when a
// widget handles the direction internally (e.g. a list at its scroll boundary).
void fl_consume_nav_up(void);
void fl_consume_nav_down(void);
void fl_consume_nav_left(void);
void fl_consume_nav_right(void);

// Programmatically move focus to the widget identified by id (e.g. restoring focus on back-navigation).
void fl_set_focus(FlLayoutId id);

// Programmatically move focus to an already-hashed element id - the form fl_input_hot_id(),
// fl_input_hashed_id() and the inspector hand out. Use when the caller holds an element id rather than the
// name+seed that produced it (UI automation walking a tree dump, for instance); prefer fl_set_focus when
// the FlLayoutId is available, since the layout id is the stable cross-frame identity.
void fl_set_focus_id(FlElementId id);

// Read this frame's navigation snapshot.
FlNavFrame fl_get_nav_frame(void);
