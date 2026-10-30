#pragma once

#include "string.h"
#include "log.h"
#include <stdarg.h>

// Initialize the error reporting system. Call once at startup, before any error_message_* call.
void error_report_init(void);

// Rewind the error arena, invalidating every message returned so far. Quiescent-only: call it at
// shutdown or teardown with no error formatting in flight on any thread. The rewind resets the
// shared bump position with no synchronization against a concurrent error_message_* call.
void error_report_cleanup(void);

// Destroy error reporting system. Call once at shutdown.
// Thread safety: Must be called from main thread only
void error_report_destroy(void);

// Format error message and optionally log it. Pass LOG_CHANNEL_INVALID as channel to skip logging.
// fmt is printf-style and supports %S for FlString; the result is allocated from a thread-safe arena.
//
// Thread safety: Safe to call from multiple threads concurrently
FlString error_message_format(LogChannelId channel, LogLevel level, const char* fmt, ...);

// As error_message_format, without logging.
FlString error_message_format_no_log(const char* fmt, ...);

// As error_message_format, taking a va_list.
FlString error_message_vformat(LogChannelId channel, LogLevel level, const char* fmt, va_list args);

// As error_message_format_no_log, taking a va_list.
FlString error_message_vformat_no_log(const char* fmt, va_list args);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convenience Macros

#define error_message_error(channel, fmt, ...) error_message_format(channel, LogLevel_Error, fmt, ##__VA_ARGS__)
#define error_message_warning(channel, fmt, ...) error_message_format(channel, LogLevel_Warning, fmt, ##__VA_ARGS__)
#define error_message_info(channel, fmt, ...) error_message_format(channel, LogLevel_Info, fmt, ##__VA_ARGS__)
