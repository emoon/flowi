#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Docking - internal C-only umbrella for the docking framework layer.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "dock/dock_workspace.h"

#ifdef __cplusplus
extern "C" {
#endif

// Declare a Detachable Panel by its stable id, under the title its tab renders, and emit its content in the
// body. The body runs at most once, and only when the panel is bound to a leaf in the active workspace and is
// that leaf's selected tab; declaring it also marks it present this frame (so an absent panel's slot stays
// reserved). The title is copied into the panel's record; FL_S("") gives a blank tab. The body is emitted INTO
// the tile the panel is docked in. A loose (mid-drag) panel's body is not emitted.
//
//   dock_panel(PANEL_INSPECTOR, FL_S("Inspector")) {
//       // ... emit the panel's widgets ...
//   }
//
// WARNING: the body must not return or break out of the scope. Both skip the loop's update expression, which
// is what closes the two layout elements the scope opened, so the rest of the frame's layout is emitted inside a
// panel that never ended. continue IS safe - it lands on the update expression.
#define dock_panel(id, title)                                                                            \
    for (int FL__DOCK_PANEL_LATCH = dock_panel_scope_begin(id, title) ? 0 : 2; FL__DOCK_PANEL_LATCH < 1; \
         FL__DOCK_PANEL_LATCH = 1, dock_panel_scope_end())

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Declarative first-run layout literals. Nest dock_split / dock_leaf to author the default tree, then
// dock_tree_build (dock_tree.h) lowers + validates it. Each macro yields a `const FlDockSpec*`; nested literals
// share the enclosing block's (or, at file scope, static) storage duration, which spans the dock_tree_build call.
//
//   static const FlDockSpec* DEFAULT_LAYOUT = dock_split(FlDockAxis_X, 0.22f,
//       dock_leaf(PANEL_NAVIGATOR),
//       dock_split(FlDockAxis_Y, 0.70f,
//           dock_leaf(PANEL_VIEWPORT),
//           dock_leaf(PANEL_CONSOLE, PANEL_LOG)));   // a tab group of two panels

// A leaf: a tab group of one-or-more panel ids (the first listed is the initial selected tab). At least one id is
// required (an empty default layout is dock_tree_build(nullptr), not dock_leaf()).
#define dock_leaf(...)                                                                                 \
    (&(const FlDockSpec) {                                                                             \
        .is_split = false,                                                                             \
        .panels = (const FlDockPanelId[]) { __VA_ARGS__ },                                             \
        .panel_count = (u32)(sizeof((const FlDockPanelId[]) { __VA_ARGS__ }) / sizeof(FlDockPanelId)), \
    })

// A split: two child specs divided along axis_ at ratio_ (the fraction the first child occupies).
#define dock_split(axis_, ratio_, child0_, child1_) \
    (&(const FlDockSpec) {                          \
        .is_split = true,                           \
        .axis = (axis_),                            \
        .ratio = (ratio_),                          \
        .child = { (child0_), (child1_) },          \
    })

#ifdef __cplusplus
}
#endif
