///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compile-only parity guard: the api_gen-generated public logging surface (<flowi/core/log.h>). Nothing is linked;
// every fl_log_* prototype must parse and match its expected signature here.
//
// The va_list fl_log_c_vmessage_loc, the LogLevel / LogChannelId internal aliases, the LOG_CHANNEL_INVALID sentinel
// and the logc_* / log_* location macros stay in src/core/log.h and are not part of this surface.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// FlString is a base type api_gen does not author - the generated header names it by value without including it,
// so it must be defined first.
#include <core/string.h>
#include <flowi/core/log.h>

#define FL_LOG_FN_MATCHES(fn, ...) \
    _Static_assert(__builtin_types_compatible_p(__typeof__(fn), __VA_ARGS__), #fn " signature drift")

FL_LOG_FN_MATCHES(fl_log_register_channel, uint32_t(FlString));
FL_LOG_FN_MATCHES(fl_log_channel_set_level, void(uint32_t, FlLogLevel));
FL_LOG_FN_MATCHES(fl_log_channel_enable, void(uint32_t, bool));
FL_LOG_FN_MATCHES(fl_log_c_message_formatted, void(uint32_t, FlLogLevel, const char*, int32_t, FlString));
FL_LOG_FN_MATCHES(fl_log_set_show_location, void(bool));
FL_LOG_FN_MATCHES(fl_log_file_init, bool(FlString));
FL_LOG_FN_MATCHES(fl_log_file_shutdown, void(void));
FL_LOG_FN_MATCHES(fl_log_file_get_path, FlString(void));
FL_LOG_FN_MATCHES(fl_log_flush, void(void));
FL_LOG_FN_MATCHES(fl_log_set_level, void(FlLogLevel));
FL_LOG_FN_MATCHES(fl_log_c_message, void(uint32_t, FlLogLevel, const char*, int32_t, const char*, ...));
FL_LOG_FN_MATCHES(fl_log_message, void(FlLogLevel, const char*, int32_t, const char*, ...));

#undef FL_LOG_FN_MATCHES

_Static_assert(sizeof(FlLogLevel) == 4, "FlLogLevel size drift");
_Static_assert(FlLogLevel_Trace == 0, "FlLogLevel_Trace value drift");
_Static_assert(FlLogLevel_Fatal == 5, "FlLogLevel_Fatal value drift");
