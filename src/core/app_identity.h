#pragma once

#include "core.h"
#include "string.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Application identity: app-flavored strings used by the log file and file watcher. They default to
// neutral flowi values; a host may override them at startup, before those modules first use them.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlAppIdentity {
    FlString data_dir_name;      // Subdirectory under $HOME for logs, e.g. ".flowi".
    FlString log_file_name;      // Log file base name, e.g. "flowi.log" (the rotated copy appends ".old").
    FlString file_watcher_queue; // macOS dispatch queue label, e.g. "org.flowi.filewatcher".
} FlAppIdentity;

// Override the identity. Non-empty fields replace the current value; empty (length 0) fields are ignored.
// Strings are copied internally, so the caller need not keep them alive.
DLL_EXPORT void fl_app_identity_set(const FlAppIdentity* identity);

// Drop every override, restoring the neutral defaults.
void app_identity_reset(void);

// Current identity fields - the host override if set, otherwise the neutral flowi default.
DLL_EXPORT FlString fl_app_identity_data_dir_name(void);
DLL_EXPORT FlString fl_app_identity_log_file_name(void);
DLL_EXPORT FlString fl_app_identity_file_watcher_queue(void);
