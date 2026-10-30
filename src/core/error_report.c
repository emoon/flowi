#include "error_report.h"
#include "arena.h"
#include "assert.h"
#include "log.h"
#include "sprintf.h"
#include <stdarg.h>
#include <stdatomic.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlArenaMt* g_error_arena = nullptr;
static u64 g_error_arena_initial_pos = 0;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void error_report_init(void) {
    if (g_error_arena != nullptr) {
        return;
    }

    g_error_arena = arena_mt_new_(&(ArenaSettings) {
        .reserved_size = MB(1),
        .allocation_site_file = __FILE__,
        .allocation_site_line = __LINE__,
    });

    // Position just past the FlArenaMt struct; error_report_cleanup rewinds to it
    g_error_arena_initial_pos = atomic_load(&g_error_arena->atomic_pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void error_report_cleanup(void) {
    FL_VALIDATE(g_error_arena != nullptr);

    FlArena* arena = g_error_arena->arena;
    atomic_store_explicit(&arena->pos, g_error_arena_initial_pos, memory_order_relaxed);
    atomic_store(&g_error_arena->atomic_pos, g_error_arena_initial_pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void error_report_destroy(void) {
    if (!g_error_arena) {
        return;
    }

    arena_mt_destroy(g_error_arena);
    g_error_arena = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString error_message_vformat(LogChannelId channel, LogLevel level, const char* fmt, va_list args) {
    FL_ASSERT_MSG(g_error_arena != nullptr, "error_message_vformat: Not initialized (call error_report_init first)");
    FL_ASSERT_MSG(fmt != nullptr, "error_message_vformat: nullptr format string");

    FlString result = vsprintf_arena_mt(g_error_arena, fmt, args);

    if (channel != LOG_CHANNEL_INVALID) {
        fl_log_c_message_formatted(channel, level, nullptr, 0, result);
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString error_message_format(LogChannelId channel, LogLevel level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FlString result = error_message_vformat(channel, level, fmt, args);
    va_end(args);
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString error_message_vformat_no_log(const char* fmt, va_list args) {
    FL_ASSERT_MSG(g_error_arena != nullptr,
                  "error_message_vformat_no_log: Not initialized (call error_report_init first)");
    FL_ASSERT_MSG(fmt != nullptr, "error_message_vformat_no_log: nullptr format string");

    return vsprintf_arena_mt(g_error_arena, fmt, args);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString error_message_format_no_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FlString result = error_message_vformat_no_log(fmt, args);
    va_end(args);
    return result;
}
