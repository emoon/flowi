// Public block-scoped container macros - the brace-block RAII sugar over the flat fl_layout_* / fl_floating_* /
// fl_scroll_area_* open/configure/close primitives.
//
//   fl_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = bg) {
//       fl_layout_area_id(FL_ID("sidebar"), .sizing = { sizing_fixed(220), sizing_grow() }) { ... }
//       fl_scroll_area_id(FL_ID("list"), .sizing = { sizing_grow(), sizing_grow() }) { ... }
//   }
//   fl_floating_area(.attach_to = FlFloatingAttachTo_Root, .z_index = 100) { ... }  // dialogs/popups/menus
//
// FlString (the type of FlLayoutId.name) is an includer-provided base type, by the same convention the
// generated public headers follow - provide it (e.g. <core/string.h>) before including this header.

#pragma once

#include <flowi/ui/layout.h>
#include <flowi/ui/ui_floating.h>
#include <flowi/ui/ui_layout.h>
#include <flowi/ui/ui_scroll.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public id constructor: build an FlLayoutId from a string literal. The string-literal forcing idiom
// ("" x "") rejects a non-literal argument at compile time.

#define FL__ID_FORCE_STRING_LITERAL(x) ("" x "")

// FL_ID("name") - id seeded purely from the name hash.
#define FL_ID(name_literal) \
    ((FlLayoutId) { .name = { .data = FL__ID_FORCE_STRING_LITERAL(name_literal), \
                              .length = sizeof(name_literal) - 1, \
                              .is_static = 1 } })

// FL_ID_INT("name", seed) - same, plus an explicit integer hash seed to disambiguate repeated names.
#define FL_ID_INT(name_literal, int_seed) \
    ((FlLayoutId) { .name = { .data = FL__ID_FORCE_STRING_LITERAL(name_literal), \
                              .length = sizeof(name_literal) - 1, \
                              .is_static = 1 }, \
                    .id = (int_seed) })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared latch driving the one-container-per-iteration RAII loop. Nesting is safe with a single shared latch:
// each inner loop runs to completion inside the outer loop's body before the outer increment fires, so the
// open/close pairs nest correctly.

static int FL__LAYOUT_LATCH = 0;

// A translation unit that includes this header without expanding a layout macro never reads the
// latch, and GCC marks it unused. Touching it here settles that; static inline so the function
// itself does not then read as unused in turn.
static inline void fl__suppress_unused_layout_latch_warning(void) {
    (void)FL__LAYOUT_LATCH;
}

// clang-format off
// Layout container with an explicit id - use when the element must be tracked (interaction, bounds queries).
#define fl_layout_area_id(id, ...) \
    for (FL__LAYOUT_LATCH = (fl_layout_start(id), fl_layout_configure_impl(((FlLayoutConfig) { __VA_ARGS__ })), 0); \
         FL__LAYOUT_LATCH < 1; \
         FL__LAYOUT_LATCH = 1, fl_layout_end())

// Layout container without an id (auto-assigned) - for pure layout containers that need no tracking.
#define fl_layout_area(...) \
    for (FL__LAYOUT_LATCH = (fl_layout_start_auto(), fl_layout_configure_impl(((FlLayoutConfig) { __VA_ARGS__ })), 0); \
         FL__LAYOUT_LATCH < 1; \
         FL__LAYOUT_LATCH = 1, fl_layout_end())

// Layout container with a pre-built config - for inline helpers that return an FlLayoutConfig.
#define fl_layout_area_cfg(cfg) \
    for (FL__LAYOUT_LATCH = (fl_layout_start_auto(), fl_layout_configure_impl(cfg), 0); \
         FL__LAYOUT_LATCH < 1; \
         FL__LAYOUT_LATCH = 1, fl_layout_end())

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Floating containers (dialogs, popups, menus) and scroll regions, same block-scoped RAII shape over the flat
// fl_floating_* / fl_scroll_area_* primitives. Floating shares the layout close primitive (fl_layout_end);
// scroll closes with its own fl_scroll_area_end.

// Floating container with an explicit id - for dialogs/popups that need a specific id (interaction, attach target).
#define fl_floating_area_id(id, ...) \
    for (FL__LAYOUT_LATCH = (fl_floating_start(id), fl_floating_configure_impl(((FlFloatingConfig) { __VA_ARGS__ })), 0); \
         FL__LAYOUT_LATCH < 1; \
         FL__LAYOUT_LATCH = 1, fl_layout_end())

// Floating container without an explicit id - seeded per call site (name + __LINE__) via the public FL_ID_INT
// constructor. A floating element does not increment its parent's child count, so the auto-id path cannot
// produce a stable id for one.
#define fl_floating_area(...) \
    fl_floating_area_id(FL_ID_INT("_Floating", __LINE__), __VA_ARGS__)

// Scroll container with an explicit id - scroll state persists across frames keyed by this id.
#define fl_scroll_area_id(id, ...) \
    for (FL__LAYOUT_LATCH = (fl_scroll_area_begin_impl(id, ((FlScrollAreaConfig) { __VA_ARGS__ })), 0); \
         FL__LAYOUT_LATCH < 1; \
         FL__LAYOUT_LATCH = 1, fl_scroll_area_end())
// clang-format on
