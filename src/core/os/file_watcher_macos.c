#include "../app_identity.h"
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static LogChannelId FW_ID = LOG_CHANNEL_INVALID;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct MacOSWatcherData {
    FSEventStreamRef stream;
    dispatch_queue_t queue;
} MacOSWatcherData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
        FlString path = string_from_cstr(paths[i]);
        u32 change_types = 0;
        bool is_directory = false;

        // FSEvents delivers events for the whole subtree; drop anything below the
        // immediate directory when the watch is non-recursive.
        if (!state->config.recursive && !file_watcher_path_is_direct_child(state->watch_path, path)) {
            continue;
        }

        FSEventStreamEventFlags flags = event_flags[i];

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

        if (flags & kFSEventStreamEventFlagItemIsDir) {
            is_directory = true;
        }

        if (is_directory && !state->config.watch_directories) {
            continue;
        }
        if (!is_directory && !state->config.watch_files) {
            continue;
        }

        if (change_types != 0) {
            logc_debug(FW_ID, "FileWatcher[%u]: Detected change: %.*s (types: 0x%x, dir: %s)", state->handle,
                       (int)path.length, path.data, change_types, is_directory ? "yes" : "no");
            add_file_change(state, path, FL_S(""), change_types, is_directory);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherError platform_start_watching(FileWatcherState* state) {
    if (FW_ID == LOG_CHANNEL_INVALID) {
        FW_ID = fl_log_register_channel(FL_S("FILE_WATCHER"));
    }

    arena_scratch_auto(temp);
    const char* cstr_path = fl_string_to_cstr(temp.arena, state->watch_path);

    // FSEvents reports every path with its symlinks resolved, and $TMPDIR on macOS is reached through one
    // (/var -> /private/var). Canonicalizing the root here is what makes the direct-child filter below, and
    // the paths this watcher reports, speak the same spelling as the events.
    char resolved[PATH_MAX];
    if (realpath(cstr_path, resolved) != nullptr) {
        state->watch_path = fl_string_copy(get_file_watcher_registry()->arena, string_from_cstr(resolved));
        cstr_path = fl_string_to_cstr(temp.arena, state->watch_path);
    }

    CFStringRef cf_path = CFStringCreateWithCString(nullptr, cstr_path, kCFStringEncodingUTF8);

    if (!cf_path) {
        return FileWatcherError_SystemError;
    }

    CFArrayRef paths_to_watch = CFArrayCreate(nullptr, (const void**)&cf_path, 1, &kCFTypeArrayCallBacks);
    CFRelease(cf_path);

    if (!paths_to_watch) {
        return FileWatcherError_SystemError;
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

    if (!stream) {
        return FileWatcherError_SystemError;
    }

    MacOSWatcherData* data = arena_alloc(get_file_watcher_registry()->arena, MacOSWatcherData);
    data->stream = stream;
    const char* queue_name = fl_string_to_cstr(temp.arena, fl_app_identity_file_watcher_queue());
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void platform_stop_watching(FileWatcherState* state) {
    if (!state || !state->platform_data) {
        return;
    }

    MacOSWatcherData* data = (MacOSWatcherData*)state->platform_data;

    if (data->stream) {
        FSEventStreamStop(data->stream);
        FSEventStreamInvalidate(data->stream);
        FSEventStreamRelease(data->stream);
    }

    if (data->queue) {
        dispatch_release(data->queue);
    }

    state->platform_data = nullptr;

    logc_debug(FW_ID, "FileWatcher[%u]: macOS FSEvents watcher stopped", state->handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherError platform_poll_changes(FileWatcherState* state) {
    if (!state || !state->platform_data || !state->is_active) {
        return FileWatcherError_Success;
    }

    MacOSWatcherData* data = (MacOSWatcherData*)state->platform_data;

    if (data->stream) {
        FSEventStreamFlushSync(data->stream);
    }

    return FileWatcherError_Success;
}

#endif // PLATFORM_MACOS
