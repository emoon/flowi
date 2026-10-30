#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel-based logging - internal include seam over the generated <flowi/core/log.h>.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "core.h"
#include "types.h"
// The generated header below names FlString by value, so its full definition must be in scope first.
#include "string.h"
#include <stdarg.h>

#include <flowi/core/log.h>
#include <flowi/core/log_macros.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal LogLevel spelling - the generated FlLogLevel, named without the Fl prefix.

typedef FlLogLevel LogLevel;

#define LogLevel_Trace FlLogLevel_Trace
#define LogLevel_Debug FlLogLevel_Debug
#define LogLevel_Info FlLogLevel_Info
#define LogLevel_Warning FlLogLevel_Warning
#define LogLevel_Error FlLogLevel_Error
#define LogLevel_Fatal FlLogLevel_Fatal

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel ids are a plain u32 in the generated surface.

typedef u32 LogChannelId;
#define LOG_CHANNEL_INVALID UINT32_MAX

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// va_list carve-out: NOT IDL-expressible, so it stays hand-written here (body in log.c).

DLL_EXPORT void fl_log_c_vmessage_loc(LogChannelId id, LogLevel level, const char* file, int line, const char* format,
                                      va_list args);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel-based logging macros that capture __FILE__ and __LINE__ at the call site

#define logc_trace(channel_id, fmt, ...) \
    fl_log_c_message(channel_id, LogLevel_Trace, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define logc_debug(channel_id, fmt, ...) \
    fl_log_c_message(channel_id, LogLevel_Debug, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define logc_info(channel_id, fmt, ...) \
    fl_log_c_message(channel_id, LogLevel_Info, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define logc_warning(channel_id, fmt, ...) \
    fl_log_c_message(channel_id, LogLevel_Warning, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define logc_error(channel_id, fmt, ...) \
    fl_log_c_message(channel_id, LogLevel_Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define logc_fatal(channel_id, fmt, ...) \
    fl_log_c_message(channel_id, LogLevel_Fatal, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel-less logging macros

#define log_trace(fmt, ...) fl_log_message(LogLevel_Trace, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) fl_log_message(LogLevel_Debug, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) fl_log_message(LogLevel_Info, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_warning(fmt, ...) fl_log_message(LogLevel_Warning, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_error(fmt, ...) fl_log_message(LogLevel_Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_fatal(fmt, ...) fl_log_message(LogLevel_Fatal, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
