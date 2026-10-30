#include "log.h"
#include "app_identity.h"
#include "arena.h"
#include "assert.h"
#include "once.h"
#include "string.h"
#include "os/os.h"
#include "sprintf.h"
#include <stdarg.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel system constants

#define MAX_LOG_CHANNELS 32
#define LOG_LEVEL_WIDTH 7 // "WARNING" is longest
#define LOG_CHANNEL_WIDTH 8

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct LogChannel {
    FlString name;
    LogLevel level; // Minimum level for this channel
    bool enabled;
    bool registered; // Slot is in use
} LogChannel;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global state

static LogLevel s_current_log_level = LogLevel_Trace;
static LogChannel s_channels[MAX_LOG_CHANNELS];
static _Atomic u32 s_channel_count = 0;           // Atomic for lock-free reads during logging
static _Atomic u32 s_max_channel_name_length = 7; // Atomic for lock-free reads during logging
static LogChannelId s_default_channel = LOG_CHANNEL_INVALID;
static bool s_show_file_line = true;

static Mutex s_channel_mutex;
static OnceFlag s_channel_mutex_initialized;

// File logging state
static i64 s_log_file_fd = -1;
static FlString s_log_file_path = { 0 };
static Mutex s_log_file_mutex;
static OnceFlag s_log_file_mutex_initialized;

// Persistent arena for log file path (lives for duration of program)
static FlArena* s_log_arena = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ensure_channel_mutex_initialized(void) {
    if (once_begin(&s_channel_mutex_initialized)) {
        mutex_init(&s_channel_mutex);
        once_end(&s_channel_mutex_initialized);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel registration and management

LogChannelId fl_log_register_channel(FlString name) {
    ensure_channel_mutex_initialized();

    mutex_lock(&s_channel_mutex);

    u32 count = atomic_load(&s_channel_count);
    if (count >= MAX_LOG_CHANNELS) {
        mutex_unlock(&s_channel_mutex);
        return LOG_CHANNEL_INVALID;
    }

    for_count(i, count) {
        if (fl_string_equals(s_channels[i].name, name)) {
            mutex_unlock(&s_channel_mutex);
            return i;
        }
    }

    // Create new channel - store first, then increment count
    // This ensures readers see a valid channel before count increases
    LogChannelId id = count;
    s_channels[id].name = name; // Assumes string persists (static strings)
    s_channels[id].level = s_current_log_level;
    s_channels[id].enabled = true;
    s_channels[id].registered = true;

    u32 max_len = atomic_load(&s_max_channel_name_length);
    if (name.length > max_len) {
        atomic_store(&s_max_channel_name_length, (u32)name.length);
    }

    // Increment count last - this is the release barrier
    atomic_store(&s_channel_count, count + 1);

    mutex_unlock(&s_channel_mutex);
    return id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_log_channel_set_level(LogChannelId id, LogLevel level) {
    ensure_channel_mutex_initialized();
    mutex_lock(&s_channel_mutex);
    if (id < atomic_load(&s_channel_count) && s_channels[id].registered) {
        s_channels[id].level = level;
    }
    mutex_unlock(&s_channel_mutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_log_channel_enable(LogChannelId id, bool enabled) {
    ensure_channel_mutex_initialized();
    mutex_lock(&s_channel_mutex);
    if (id < atomic_load(&s_channel_count) && s_channels[id].registered) {
        s_channels[id].enabled = enabled;
    }
    mutex_unlock(&s_channel_mutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Copies a channel's name/level/enabled under s_channel_mutex so concurrent set_level / enable writers can't be
// observed torn. Invalid or unregistered ids resolve to a permissive DEFAULT channel.
static void channel_info_read(LogChannelId id, FlString* name, LogLevel* level, bool* enabled) {
    ensure_channel_mutex_initialized();
    mutex_lock(&s_channel_mutex);
    u32 count = atomic_load(&s_channel_count);
    if (id >= count || !s_channels[id].registered || id == LOG_CHANNEL_INVALID) {
        *name = FL_S("DEFAULT");
        *level = LogLevel_Debug;
        *enabled = true;
    } else {
        *name = s_channels[id].name;
        *level = s_channels[id].level;
        *enabled = s_channels[id].enabled;
    }
    mutex_unlock(&s_channel_mutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_log_set_show_location(bool show) {
    s_show_file_line = show;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File logging implementation

static void ensure_log_file_mutex_initialized(void) {
    if (once_begin(&s_log_file_mutex_initialized)) {
        mutex_init(&s_log_file_mutex);
        once_end(&s_log_file_mutex_initialized);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_log_file_init(FlString base_dir) {
    ensure_log_file_mutex_initialized();

    if (s_log_arena == nullptr) {
        s_log_arena = arena_new();
    }

    FlString dir_path;
    if (base_dir.length > 0) {
        dir_path = fl_string_copy(s_log_arena, base_dir);
    } else {
        const char* home = getenv_os("HOME");
        if (home == nullptr) {
            return false;
        }
        FlString home_str = string_from_cstr(home);
        dir_path = file_build_path_os(s_log_arena, home_str, fl_app_identity_data_dir_name());
    }

    if (!file_directory_exists_os(dir_path)) {
        if (!file_create_directory_recursive_os(dir_path)) {
            return false;
        }
    }

    FlString log_name = fl_app_identity_log_file_name();
    FlString log_path = file_build_path_os(s_log_arena, dir_path, log_name);
    FlString old_log_path
        = file_build_path_os(s_log_arena, dir_path, fl_sprintf_arena(s_log_arena, "%S.old", log_name));

    // Rotation overwrites the previous .old
    if (file_exists_os(log_path)) {
        file_rename_os(log_path, old_log_path);
    }

    // Close the fd from any previous init so re-init doesn't leak it
    mutex_lock(&s_log_file_mutex);
    if (s_log_file_fd >= 0) {
        file_close_handle_os(s_log_file_fd);
    }
    s_log_file_fd = file_open_write_os(log_path, false);
    bool opened = s_log_file_fd >= 0;
    if (opened) {
        s_log_file_path = log_path;
    }
    mutex_unlock(&s_log_file_mutex);

    return opened;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_log_file_shutdown(void) {
    ensure_log_file_mutex_initialized();

    mutex_lock(&s_log_file_mutex);
    if (s_log_file_fd >= 0) {
        file_close_handle_os(s_log_file_fd);
        s_log_file_fd = -1;
    }
    mutex_unlock(&s_log_file_mutex);

    // s_log_arena is kept alive so the path stays readable after shutdown
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_log_file_get_path(void) {
    ensure_log_file_mutex_initialized();

    mutex_lock(&s_log_file_mutex);
    FlString path = s_log_file_path;
    mutex_unlock(&s_log_file_mutex);
    return path;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void log_file_write(FlString message) {
    if (s_log_file_fd < 0) {
        return;
    }

    ensure_log_file_mutex_initialized();

    mutex_lock(&s_log_file_mutex);
    if (s_log_file_fd >= 0) {
        file_write_to_handle_os(s_log_file_fd, (const u8*)message.data, (i64)message.length);
    }
    mutex_unlock(&s_log_file_mutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString format_channel_padded(FlArena* arena, FlString channel_name) {
    return fl_sprintf_arena(arena, "%-*S", atomic_load(&s_max_channel_name_length), channel_name);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString get_level_string(LogLevel level) {
    switch (level) {
        case LogLevel_Trace:
            return FL_S("TRACE");
        case LogLevel_Debug:
            return FL_S("DEBUG");
        case LogLevel_Info:
            return FL_S("INFO");
        case LogLevel_Warning:
            return FL_S("WARNING");
        case LogLevel_Error:
            return FL_S("ERROR");
        case LogLevel_Fatal:
            return FL_S("FATAL");
        default:
            return FL_S("UNKNOWN");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString format_level_padded(FlArena* arena, LogLevel level) {
    FlString level_str = get_level_string(level);
    FlString color_code;
    switch (level) {
        case LogLevel_Trace:
            color_code = FL_S("\033[30m");
            break; // Dark
        case LogLevel_Debug:
            color_code = FL_S("\033[36m");
            break; // Cyan
        case LogLevel_Info:
            color_code = FL_S("\033[32m");
            break; // Green
        case LogLevel_Warning:
            color_code = FL_S("\033[33m");
            break; // Yellow
        case LogLevel_Error:
            color_code = FL_S("\033[31m");
            break; // Red
        case LogLevel_Fatal:
            color_code = FL_S("\033[35m");
            break; // Magenta
        default:
            color_code = FL_S("\033[0m");
            break; // Reset
    }
    return fl_sprintf_arena(arena, "%S%-*S\033[0m", color_code, LOG_LEVEL_WIDTH, level_str);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString format_level_plain(FlArena* arena, LogLevel level) {
    return fl_sprintf_arena(arena, "%-*S", LOG_LEVEL_WIDTH, get_level_string(level));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString format_log_line(FlArena* arena, FlString timestamp, FlString channel, FlString level, FlString message,
                                const char* filename, int line, bool show_location) {
    if (show_location && filename) {
        return fl_sprintf_arena(arena, "[%S] [%S] [%S] : %S (%s:%d)\n", timestamp, channel, level, message, filename,
                                line);
    }
    return fl_sprintf_arena(arena, "[%S] [%S] [%S] : %S\n", timestamp, channel, level, message);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void log_formatted_message_internal(LogChannelId id, LogLevel level, const char* file, int line,
                                           FlString message) {
    FlString channel_name;
    LogLevel channel_level;
    bool channel_enabled;
    channel_info_read(id, &channel_name, &channel_level, &channel_enabled);

    if (!channel_enabled || level < channel_level) {
        return;
    }

    arena_scratch_auto(temp);

    i64 timestamp = get_current_time_us_os();
    FlString timestamp_str = timestamp_format_os(temp.arena, timestamp);
    FlString channel_padded = format_channel_padded(temp.arena, channel_name);

    const char* filename = nullptr;
    if (file) {
        filename = strrchr(file, '/');
        filename = filename ? filename + 1 : file;
    }

    // Format and write to stderr (with colors)
    FlString level_colored = format_level_padded(temp.arena, level);
    FlString stderr_msg = format_log_line(temp.arena, timestamp_str, channel_padded, level_colored, message, filename,
                                          line, s_show_file_line);
    stderr_write_os(stderr_msg);

    // Format and write to file (without colors)
    if (s_log_file_fd >= 0) {
        FlString level_plain = format_level_plain(temp.arena, level);
        FlString file_msg = format_log_line(temp.arena, timestamp_str, channel_padded, level_plain, message, filename,
                                            line, s_show_file_line);
        log_file_write(file_msg);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_log_c_vmessage_loc(LogChannelId id, LogLevel level, const char* file, int line, const char* format,
                           va_list args) {
    FlString channel_name;
    LogLevel channel_level;
    bool channel_enabled;
    channel_info_read(id, &channel_name, &channel_level, &channel_enabled);

    if (!channel_enabled || level < channel_level) {
        return;
    }

    arena_scratch_auto(temp);
    FlString message = vsprintf_arena(temp.arena, format, args);

    log_formatted_message_internal(id, level, file, line, message);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_log_c_message(LogChannelId id, LogLevel level, const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    fl_log_c_vmessage_loc(id, level, file, line, format, args);
    va_end(args);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_log_c_message_formatted(LogChannelId id, LogLevel level, const char* file, int line, FlString message) {
    log_formatted_message_internal(id, level, file, line, message);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ensure_default_channel(void) {
    if (s_default_channel == LOG_CHANNEL_INVALID) {
        s_default_channel = fl_log_register_channel(FL_S("CORE"));
        fl_log_channel_set_level(s_default_channel, s_current_log_level);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_log_message(LogLevel level, const char* file, int line, const char* format, ...) {
    ensure_default_channel();

    va_list args;
    va_start(args, format);
    fl_log_c_vmessage_loc(s_default_channel, level, file, line, format, args);
    va_end(args);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_log_flush(void) {}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_log_set_level(LogLevel level) {
    s_current_log_level = level;
    ensure_default_channel();

    ensure_channel_mutex_initialized();
    mutex_lock(&s_channel_mutex);
    u32 count = atomic_load(&s_channel_count);
    for (u32 i = 0; i < count; i++) {
        if (s_channels[i].registered) {
            s_channels[i].level = level;
        }
    }
    mutex_unlock(&s_channel_mutex);
}
