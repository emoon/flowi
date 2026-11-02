#pragma once

// Application identity: the app-flavored names flowi uses for its log file, its per-user data directory and
// its macOS file-watcher queue. They default to neutral flowi values; a host may override them at startup,
// before those modules first use them.

#include <flowi/core/string.h>
#include <flowi/fl_api.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlAppIdentity {
    // Subdirectory under $HOME for logs, e.g. ".flowi".
    FlString data_dir_name;
    // Log file base name, e.g. "flowi.log"; the rotated copy appends ".old".
    FlString log_file_name;
    // macOS dispatch queue label, e.g. "org.flowi.filewatcher".
    FlString file_watcher_queue;
} FlAppIdentity;

// Override the identity. Non-empty fields replace the current value; empty fields (length 0) are ignored. The
// strings are copied, so the caller need not keep them alive.
FL_API void fl_app_identity_set(const FlAppIdentity* identity);

// The current fields: the host override if one was set, otherwise the neutral flowi default.
FL_API FlString fl_app_identity_data_dir_name(void);
FL_API FlString fl_app_identity_log_file_name(void);
FL_API FlString fl_app_identity_file_watcher_queue(void);

#ifdef __cplusplus
}
#endif
