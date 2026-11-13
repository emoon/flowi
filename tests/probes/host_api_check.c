// Compile-only parity guard: the api_gen-generated public embedded-host surface (<flowi/app/host.h>). Nothing is
// linked; every prototype must parse and match its expected signature here.

#include <stdbool.h>
#include <stdint.h>

#include <flowi/app/host.h>

_Static_assert(sizeof(FlHost*) == sizeof(void*), "FlHost must be an opaque handle, crossed only by pointer");

#define FL_HOST_FIELD_MATCHES(type, field, expected) \
    _Static_assert(__builtin_types_compatible_p(__typeof__(((type*)0)->field), expected), #type "::" #field " drift")

FL_HOST_FIELD_MATCHES(FlHostSettings, render_width, int32_t);
FL_HOST_FIELD_MATCHES(FlHostSettings, render_height, int32_t);
FL_HOST_FIELD_MATCHES(FlHostSettings, mount_working_directory, bool);

#undef FL_HOST_FIELD_MATCHES

#define FL_HOST_FN_MATCHES(fn, ...) \
    _Static_assert(__builtin_types_compatible_p(__typeof__(fn), __VA_ARGS__), #fn " signature drift")

FL_HOST_FN_MATCHES(fl_host_create, FlHost*(FlArena*, const FlHostSettings*));
FL_HOST_FN_MATCHES(fl_host_destroy, void(FlHost*));
FL_HOST_FN_MATCHES(fl_host_frame_update, void(FlHost*, double));
FL_HOST_FN_MATCHES(fl_host_read_pixels, bool(FlHost*, FlPixelReadback*));

#undef FL_HOST_FN_MATCHES
