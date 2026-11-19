#pragma once

#include "core.h"
#include "string.h"

// App-specific strings used by the log file and the file watcher. They default to neutral flowi values;
// fl_app_create overrides them from FlAppSettings before those modules first use them.

// Non-empty strings replace the current value; empty (length 0) strings are ignored. The strings are copied
// internally, so the caller need not keep them alive.
void app_identity_set(FlString data_dir_name, FlString log_file_name, FlString file_watcher_queue);

// Drop every override, restoring the neutral defaults.
void app_identity_reset(void);

// Subdirectory under $HOME for logs, e.g. ".flowi".
FlString app_identity_data_dir_name(void);
// Log file base name, e.g. "flowi.log" (the rotated copy appends ".old").
FlString app_identity_log_file_name(void);
// macOS dispatch queue label, e.g. "org.flowi.filewatcher".
FlString app_identity_file_watcher_queue(void);
