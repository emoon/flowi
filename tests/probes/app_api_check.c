///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compile-only parity guard: the api_gen-generated public app surface (<flowi/app/app.h>). Nothing is linked;
// every prototype must parse and match its expected signature here.
//
// The mouse button crosses as int32_t - the public spelling of the FlMouseButton discriminant, a private
// platform enum (src/platform/input_types.h). int32_t is ABI-identical (the enum is int-sized) and
// matches the raw sys binding's c_int.

#include <stdbool.h>
#include <stdint.h>

// FlString is a base type api_gen does not author - the generated headers name it without including it, so it
// must be defined before them.
#include <core/string.h>
#include <flowi/app/app.h>

_Static_assert(sizeof(FlApp*) == sizeof(void*), "FlApp must be an opaque handle, crossed only by pointer");
_Static_assert(sizeof(FlAppFrameFn) == sizeof(void (*)(void*)), "FlAppFrameFn must be a plain function pointer");

#define FL_APP_FN_MATCHES(fn, ...) \
    _Static_assert(__builtin_types_compatible_p(__typeof__(fn), __VA_ARGS__), #fn " signature drift")

FL_APP_FN_MATCHES(fl_app_create, FlApp*(const FlAppSettings*));
FL_APP_FN_MATCHES(fl_app_destroy, void(FlApp*));
FL_APP_FN_MATCHES(fl_app_tick, bool(FlApp*, void*, FlAppFrameFn));
FL_APP_FN_MATCHES(fl_app_run, void(FlApp*, void*, FlAppFrameFn));
FL_APP_FN_MATCHES(fl_app_set_ui_zoom, void(FlApp*, float));
FL_APP_FN_MATCHES(fl_app_inject_mouse_move, void(FlApp*, int32_t, int32_t));
FL_APP_FN_MATCHES(fl_app_inject_mouse_button, void(FlApp*, FlMouseButton, bool));
FL_APP_FN_MATCHES(fl_app_inject_mouse_delta, void(FlApp*, float, float));
FL_APP_FN_MATCHES(fl_app_inject_wheel, void(FlApp*, float, float));
FL_APP_FN_MATCHES(fl_app_inject_key, void(FlApp*, FlKey, bool));
FL_APP_FN_MATCHES(fl_app_inject_text, void(FlApp*, FlString));
FL_APP_FN_MATCHES(fl_app_inject_window_close, void(FlApp*));
FL_APP_FN_MATCHES(fl_app_headless_cursor_shape, FlCursorShape(FlApp*));
FL_APP_FN_MATCHES(fl_app_headless_relative_mouse_mode, bool(FlApp*));
FL_APP_FN_MATCHES(fl_app_headless_last_warp, FlVec2(FlApp*));
FL_APP_FN_MATCHES(fl_app_set_test_dt, void(FlApp*, float));
FL_APP_FN_MATCHES(fl_app_read_pixels, bool(FlApp*, FlPixelReadback*));

#undef FL_APP_FN_MATCHES
