///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Standalone compile guard for the hand-written public nav/action query surface (<flowi/ui/nav.h>).
//
// No .def single-sources this surface: the C header and the Rust binding (rust/flowi/src/sys/nav.rs) are both
// hand-written, so FlNavFrame is hand-packed twice and the two spellings can silently diverge. This TU links
// nothing; it proves at compile time that nav.h is self-contained valid C given only FlString, that every fl_*
// prototype keeps its exact signature, and that FlNavFrame keeps the size, alignment and field offsets the
// const _ block in rust/flowi/src/sys/nav.rs duplicates verbatim.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// FlString - includer-provided base type api_gen does not author.
#include <core/string.h>

#include <flowi/ui/nav.h>

// Per-prototype signature identity. A drift in the header (reordered/retyped arg, changed return) fails the build.
#define FL_NAV_FN_MATCHES(fn, ...) \
    _Static_assert(__builtin_types_compatible_p(__typeof__(fn), __VA_ARGS__), #fn " signature drift")

FL_NAV_FN_MATCHES(fl_action_pressed, bool(FlAction));
FL_NAV_FN_MATCHES(fl_nav_up_pressed, bool(void));
FL_NAV_FN_MATCHES(fl_nav_down_pressed, bool(void));
FL_NAV_FN_MATCHES(fl_consume_nav_up, void(void));
FL_NAV_FN_MATCHES(fl_consume_nav_down, void(void));
FL_NAV_FN_MATCHES(fl_consume_nav_left, void(void));
FL_NAV_FN_MATCHES(fl_consume_nav_right, void(void));
FL_NAV_FN_MATCHES(fl_set_focus, void(FlLayoutId));
FL_NAV_FN_MATCHES(fl_set_focus_id, void(FlElementId));
FL_NAV_FN_MATCHES(fl_get_nav_frame, FlNavFrame(void));

// FlNavFrame layout, pinned against the hand-packed Rust mirror in rust/flowi/src/sys/nav.rs.
_Static_assert(sizeof(FlNavFrame) == 8, "FlNavFrame size drift vs the Rust mirror");
_Static_assert(_Alignof(FlNavFrame) == 4, "FlNavFrame align drift vs the Rust mirror");
_Static_assert(offsetof(FlNavFrame, focused_id) == 0, "FlNavFrame.focused_id offset drift vs the Rust mirror");
_Static_assert(offsetof(FlNavFrame, nav_left) == 4, "FlNavFrame.nav_left offset drift vs the Rust mirror");
_Static_assert(offsetof(FlNavFrame, nav_right) == 5, "FlNavFrame.nav_right offset drift vs the Rust mirror");
_Static_assert(offsetof(FlNavFrame, nav_up) == 6, "FlNavFrame.nav_up offset drift vs the Rust mirror");
_Static_assert(offsetof(FlNavFrame, nav_down) == 7, "FlNavFrame.nav_down offset drift vs the Rust mirror");

// The Rust binding reads focused_id as u32 and the four pending-nav flags as bool; pin those widths too.
_Static_assert(sizeof(((FlNavFrame*)0)->focused_id) == 4, "FlNavFrame.focused_id width drift vs the Rust mirror");
_Static_assert(sizeof(((FlNavFrame*)0)->nav_left) == 1, "FlNavFrame.nav_left width drift vs the Rust mirror");
