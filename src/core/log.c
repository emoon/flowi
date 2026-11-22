#include "log.h"
#include "arena.h"
#include "assert.h"
#include "once.h"
#include "string.h"
#include "os/os.h"
#include "sprintf.h"
#include <stdarg.h>
#include <string.h>

// Channel system constants

#define MAX_LOG_CHANNELS 32
#define LOG_LEVEL_WIDTH 7 // "WARNING" is longest
#define LOG_CHANNEL_WIDTH 8

typedef struct LogChannel {
    FlString name;
    LogLevel level; // Minimum level for this channel
    bool enabled;
    bool registered; // Slot is in use
} LogChannel;

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

static void ensure_channel_mutex_initialized(void) {
    if (once_begin(&s_channel_mutex_initialized)) {
        mutex_init(&s_channel_mutex);
        once_end(&s_channel_mutex_initialized);
    }
}

// Channel registration and management

LogChannelId fl_log_register_channel(FlString name) {
    ensure_channel_mutex_initialized();

    mutex_lock(&s_channel_mutex);

    u32 count = atomic_load(&s_channel_count);
    for_count(i, count) {
        if (fl_string_equals(s_channels[i].name, name)) {
            mutex_unlock(&s_channel_mutex);
            return i;
        }
    }

    if (count >= MAX_LOG_CHANNELS) {
        mutex_unlock(&s_channel_mutex);
        return LOG_CHANNEL_INVALID;
    }

    // Fill in the channel before incrementing the count, so lock-free readers never see a count
    // that covers an unwritten slot.
    LogChannelId id = count;
    s_channels[id].name = name; // Assumes string persists (static strings)
    s_channels[id].level = s_current_log_level;
    s_channels[id].enabled = true;
    s_channels[id].registered = true;

    u32 max_len = atomic_load(&s_max_channel_name_length);
    if (name.length > max_len) {
        atomic_store(&s_max_channel_name_length, (u32)name.length);
    }

    // The count is stored last; it publishes the channel to readers.
    atomic_store(&s_channel_count, count + 1);

    mutex_unlock(&s_channel_mutex);
    return id;
}

void fl_log_channel_set_level(LogChannelId id, LogLevel level) {
    ensure_channel_mutex_initialized();
    mutex_lock(&s_channel_mutex);
    if (id < atomic_load(&s_channel_count) && s_channels[id].registered) {
        s_channels[id].level = level;
    }
    mutex_unlock(&s_channel_mutex);
}

void fl_log_channel_enable(LogChannelId id, bool enabled) {
    ensure_channel_mutex_initialized();
    mutex_lock(&s_channel_mutex);
    if (id < atomic_load(&s_channel_count) && s_channels[id].registered) {
        s_channels[id].enabled = enabled;
    }
    mutex_unlock(&s_channel_mutex);
}

typedef struct {
    FlString name;
    LogLevel level;
    bool enabled;
} ChannelInfo;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool channel_is_resolvable(LogChannelId id, u32 count) {
    return id < count && s_channels[id].registered && id != LOG_CHANNEL_INVALID;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Copies a channel's name/level/enabled under s_channel_mutex so concurrent set_level / enable writers can't be
// observed torn. Invalid or unregistered ids resolve to a permissive DEFAULT channel.
static ChannelInfo channel_info_read(LogChannelId id) {
    ensure_channel_mutex_initialized();
    mutex_lock(&s_channel_mutex);
    u32 count = atomic_load(&s_channel_count);
    ChannelInfo info = { .name = FL_S("DEFAULT"), .level = LogLevel_Debug, .enabled = true };
    if (channel_is_resolvable(id, count)) {
        info = (ChannelInfo) { s_channels[id].name, s_channels[id].level, s_channels[id].enabled };
    }
    mutex_unlock(&s_channel_mutex);
    return info;
}

void fl_log_set_show_location(bool show) {
    s_show_file_line = show;
}

// File logging implementation

static void ensure_log_file_mutex_initialized(void) {
    if (once_begin(&s_log_file_mutex_initialized)) {
        mutex_init(&s_log_file_mutex);
        once_end(&s_log_file_mutex_initialized);
    }
}

static bool resolve_log_dir(FlString base_dir, FlString* dir_path) {
    if (base_dir.length > 0) {
        *dir_path = fl_string_copy(s_log_arena, base_dir);
        return true;
    }

    const char* home = getenv_os("HOME");
    if (home == nullptr) {
        return false;
    }

    *dir_path = file_build_path_os(s_log_arena, string_from_cstr(home), FL_S(".flowi"));
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ensure_log_dir_exists(FlString dir_path) {
    return fl_os_directory_exists(dir_path) || fl_os_create_directory_recursive(dir_path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void rotate_log_file(FlString dir_path, FlString log_name, FlString log_path) {
    FlString old_log_path
        = file_build_path_os(s_log_arena, dir_path, fl_sprintf_arena(s_log_arena, "%S.old", log_name));

    // Rotation overwrites the previous .old
    if (file_exists_os(log_path)) {
        file_rename_os(log_path, old_log_path);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool open_log_file(FlString log_path) {
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

bool fl_log_file_init(FlString base_dir) {
    ensure_log_file_mutex_initialized();

    if (s_log_arena == nullptr) {
        s_log_arena = arena_new();
    }

    FlString dir_path;
    if (!resolve_log_dir(base_dir, &dir_path)) {
        return false;
    }

    if (!ensure_log_dir_exists(dir_path)) {
        return false;
    }

    FlString log_name = FL_S("flowi.log");
    FlString log_path = file_build_path_os(s_log_arena, dir_path, log_name);
    rotate_log_file(dir_path, log_name, log_path);

    return open_log_file(log_path);
}

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

FlString fl_log_file_get_path(void) {
    ensure_log_file_mutex_initialized();

    mutex_lock(&s_log_file_mutex);
    FlString path = s_log_file_path;
    mutex_unlock(&s_log_file_mutex);
    return path;
}

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

static FlString format_channel_padded(FlArena* arena, FlString channel_name) {
    return fl_sprintf_arena(arena, "%-*S", atomic_load(&s_max_channel_name_length), channel_name);
}

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

static FlString format_level_plain(FlArena* arena, LogLevel level) {
    return fl_sprintf_arena(arena, "%-*S", LOG_LEVEL_WIDTH, get_level_string(level));
}

typedef struct {
    FlString timestamp;
    FlString channel;
    FlString level;
    FlString message;
    const char* filename;
    int line;
    bool show_location;
} LogLine;

typedef struct {
    const char* file;
    int line;
} LogSite;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString format_log_line(FlArena* arena, const LogLine* l) {
    if (l->show_location && l->filename) {
        return fl_sprintf_arena(arena, "[%S] [%S] [%S] : %S (%s:%d)\n", l->timestamp, l->channel, l->level,
                                l->message, l->filename, l->line);
    }
    return fl_sprintf_arena(arena, "[%S] [%S] [%S] : %S\n", l->timestamp, l->channel, l->level, l->message);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* path_basename(const char* file) {
    if (file == nullptr) {
        return nullptr;
    }

    const char* slash = strrchr(file, '/');
    return slash ? slash + 1 : file;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void log_formatted_message_internal(LogChannelId id, LogLevel level, LogSite site, FlString message) {
    ChannelInfo channel = channel_info_read(id);

    if (!channel.enabled || level < channel.level) {
        return;
    }

    arena_scratch_auto(temp);

    LogLine line = {
        .timestamp = timestamp_format_os(temp.arena, fl_os_time_us()),
        .channel = format_channel_padded(temp.arena, channel.name),
        .message = message,
        .filename = path_basename(site.file),
        .line = site.line,
        .show_location = s_show_file_line,
    };

    // Format and write to stderr (with colors)
    line.level = format_level_padded(temp.arena, level);
    stderr_write_os(format_log_line(temp.arena, &line));

    // Format and write to file (without colors)
    if (s_log_file_fd >= 0) {
        line.level = format_level_plain(temp.arena, level);
        log_file_write(format_log_line(temp.arena, &line));
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_log_c_vmessage_loc(LogChannelId id, LogLevel level, const char* file, int line, const char* format,
                           va_list args) {
    ChannelInfo channel = channel_info_read(id);

    if (!channel.enabled || level < channel.level) {
        return;
    }

    arena_scratch_auto(temp);
    FlString message = vsprintf_arena(temp.arena, format, args);

    log_formatted_message_internal(id, level, (LogSite) { file, line }, message);
}

void fl_log_c_message(LogChannelId id, LogLevel level, const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    fl_log_c_vmessage_loc(id, level, file, line, format, args);
    va_end(args);
}

void fl_log_c_message_formatted(LogChannelId id, LogLevel level, const char* file, int line, FlString message) {
    log_formatted_message_internal(id, level, (LogSite) { file, line }, message);
}

static void ensure_default_channel(void) {
    if (s_default_channel == LOG_CHANNEL_INVALID) {
        s_default_channel = fl_log_register_channel(FL_S("CORE"));
        fl_log_channel_set_level(s_default_channel, s_current_log_level);
    }
}

void fl_log_message(LogLevel level, const char* file, int line, const char* format, ...) {
    ensure_default_channel();

    va_list args;
    va_start(args, format);
    fl_log_c_vmessage_loc(s_default_channel, level, file, line, format, args);
    va_end(args);
}

void fl_log_flush(void) {}

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
