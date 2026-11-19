#include "../arena.h"
#include "../file_watcher_internal.h"
#include "../fixed_array.h"
#include "../log.h"
#include "../path.h"
#include "../string.h"

#if PLATFORM_MACOS

#include <CoreServices/CoreServices.h>
#include <limits.h>
#include <stdlib.h>

static LogChannelId FW_ID = LOG_CHANNEL_INVALID;

typedef struct MacOSWatcherData {
    FSEventStreamRef stream;
    dispatch_queue_t queue;
} MacOSWatcherData;

static u32 change_types_from_flags(FSEventStreamEventFlags flags) {
    u32 change_types = 0;

    if (flags & kFSEventStreamEventFlagItemCreated) {
        change_types |= FlFileChangeType_Created;
    }
    if (flags & kFSEventStreamEventFlagItemModified) {
        change_types |= FlFileChangeType_Modified;
    }
    if (flags & kFSEventStreamEventFlagItemRemoved) {
        change_types |= FlFileChangeType_Deleted;
    }
    if (flags & kFSEventStreamEventFlagItemRenamed) {
        change_types |= FlFileChangeType_Moved;
    }

    return change_types;
}

static bool is_watched_kind(const FlFileWatcherConfig* config, bool is_directory) {
    return is_directory ? config->watch_directories : config->watch_files;
}

static void handle_fsevent(FileWatcherState* state, FlString path, FSEventStreamEventFlags flags) {
    // FSEvents delivers events for the whole subtree; drop anything below the
    // immediate directory when the watch is non-recursive.
    if (!state->config.recursive && !file_watcher_path_is_direct_child(state->watch_path, path)) {
        return;
    }

    bool is_directory = (flags & kFSEventStreamEventFlagItemIsDir) != 0;
    if (!is_watched_kind(&state->config, is_directory)) {
        return;
    }

    u32 change_types = change_types_from_flags(flags);
    if (change_types == 0) {
        return;
    }

    logc_debug(FW_ID, "FileWatcher[%u]: Detected change: %.*s (types: 0x%x, dir: %s)", state->handle, (int)path.length,
               path.data, change_types, is_directory ? "yes" : "no");
    add_file_change(state, path, FL_S(""), change_types, is_directory);
}

static void fsevent_callback(ConstFSEventStreamRef stream_ref, void* callback_info, size_t num_events,
                             void* event_paths, const FSEventStreamEventFlags event_flags[],
                             const FSEventStreamEventId event_ids[]) {
    (void)stream_ref;
    (void)event_ids;

    FileWatcherState* state = (FileWatcherState*)callback_info;
    if (!state || !state->is_active) {
        return;
    }

    char** paths = (char**)event_paths;

    for_count(i, num_events) {
        handle_fsevent(state, string_from_cstr(paths[i]), event_flags[i]);
    }
}

// FSEvents reports every path with its symlinks resolved, and $TMPDIR on macOS is reached through one
// (/var -> /private/var). Canonicalize the root so the direct-child filter and the paths this
// watcher reports use the same spelling as the events.
static const char* canonicalize_watch_path(FileWatcherState* state, FlArena* temp_arena) {
    const char* cstr_path = fl_string_to_cstr(temp_arena, state->watch_path);

    char resolved[PATH_MAX];
    if (realpath(cstr_path, resolved) == nullptr) {
        return cstr_path;
    }

    state->watch_path = fl_string_copy(get_file_watcher_registry()->arena, string_from_cstr(resolved));
    return fl_string_to_cstr(temp_arena, state->watch_path);
}

// Returns nullptr on failure.
static FSEventStreamRef create_event_stream(FileWatcherState* state, const char* cstr_path) {
    CFStringRef cf_path = CFStringCreateWithCString(nullptr, cstr_path, kCFStringEncodingUTF8);
    if (!cf_path) {
        return nullptr;
    }

    CFArrayRef paths_to_watch = CFArrayCreate(nullptr, (const void**)&cf_path, 1, &kCFTypeArrayCallBacks);
    CFRelease(cf_path);

    if (!paths_to_watch) {
        return nullptr;
    }

    FSEventStreamContext context = { 0 };
    context.info = state;

    // FSEvents streams are inherently recursive over the watched path (there is no
    // shallow-only flag); recursive=false is honored by filtering events down to
    // direct children in fsevent_callback.
    FSEventStreamCreateFlags flags = kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer;

    // 0.05 is the latency argument, in seconds: 50ms of event coalescing
    FSEventStreamRef stream = FSEventStreamCreate(nullptr, &fsevent_callback, &context, paths_to_watch,
                                                  kFSEventStreamEventIdSinceNow, 0.05, flags);

    CFRelease(paths_to_watch);
    return stream;
}

FileWatcherError platform_start_watching(FileWatcherState* state) {
    if (FW_ID == LOG_CHANNEL_INVALID) {
        FW_ID = fl_log_register_channel(FL_S("FILE_WATCHER"));
    }

    arena_scratch_auto(temp);
    const char* cstr_path = canonicalize_watch_path(state, temp.arena);

    FSEventStreamRef stream = create_event_stream(state, cstr_path);
    if (!stream) {
        return FileWatcherError_SystemError;
    }

    MacOSWatcherData* data = arena_alloc(get_file_watcher_registry()->arena, MacOSWatcherData);
    data->stream = stream;
    const char* queue_name = fl_string_to_cstr(temp.arena, get_file_watcher_registry()->queue_label);
    data->queue = dispatch_queue_create(queue_name, DISPATCH_QUEUE_SERIAL);

    if (!data->queue) {
        FSEventStreamRelease(stream);
        return FileWatcherError_SystemError;
    }

    FSEventStreamSetDispatchQueue(stream, data->queue);

    if (!FSEventStreamStart(stream)) {
        dispatch_release(data->queue);
        FSEventStreamRelease(stream);
        return FileWatcherError_SystemError;
    }

    state->platform_data = data;

    logc_debug(FW_ID, "FileWatcher[%u]: macOS FSEvents watcher initialized", state->handle);

    return FileWatcherError_Success;
}

static void drain_queue_noop(void* context) {
    (void)context;
}

void platform_stop_watching(FileWatcherState* state) {
    if (!state || !state->platform_data) {
        return;
    }

    MacOSWatcherData* data = (MacOSWatcherData*)state->platform_data;

    if (data->stream) {
        FSEventStreamStop(data->stream);
        FSEventStreamInvalidate(data->stream);
    }

    // Returns once every callback already queued or running on the stream's serial queue has finished.
    if (data->queue) {
        dispatch_sync_f(data->queue, nullptr, drain_queue_noop);
    }

    if (data->stream) {
        FSEventStreamRelease(data->stream);
    }

    if (data->queue) {
        dispatch_release(data->queue);
    }

    state->platform_data = nullptr;

    logc_debug(FW_ID, "FileWatcher[%u]: macOS FSEvents watcher stopped", state->handle);
}

static bool is_pollable(const FileWatcherState* state) {
    if (!state || !state->is_active) {
        return false;
    }
    return state->platform_data != nullptr;
}

FileWatcherError platform_poll_changes(FileWatcherState* state) {
    if (!is_pollable(state)) {
        return FileWatcherError_Success;
    }

    MacOSWatcherData* data = (MacOSWatcherData*)state->platform_data;

    if (data->stream) {
        FSEventStreamFlushSync(data->stream);
    }

    return FileWatcherError_Success;
}

#endif // PLATFORM_MACOS
