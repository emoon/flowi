#pragma once

// Channel-less logging macros - the public convenience surface over fl_log_message. They
// capture __FILE__/__LINE__ at the call site.
//
// This is the one header a caller needs for logging: it pulls the whole logging surface
// below. A caller that wants its own channel instead of the default one uses the
// fl_log_c_message family with an id from fl_log_register_channel.

// The generated <flowi/core/log.h> declares FlLogLevel and the `fl_log_*` prototypes. FlString
// is includer-provided by api_gen convention and a prototype names it by value, so it comes first.
#include <flowi/core/string.h>
#include <flowi/core/log.h>

#define fl_log_trace(...) fl_log_message(FlLogLevel_Trace, __FILE__, __LINE__, __VA_ARGS__)
#define fl_log_debug(...) fl_log_message(FlLogLevel_Debug, __FILE__, __LINE__, __VA_ARGS__)
#define fl_log_info(...) fl_log_message(FlLogLevel_Info, __FILE__, __LINE__, __VA_ARGS__)
#define fl_log_warning(...) fl_log_message(FlLogLevel_Warning, __FILE__, __LINE__, __VA_ARGS__)
#define fl_log_error(...) fl_log_message(FlLogLevel_Error, __FILE__, __LINE__, __VA_ARGS__)
#define fl_log_fatal(...) fl_log_message(FlLogLevel_Fatal, __FILE__, __LINE__, __VA_ARGS__)
