#include "app_identity.h"
#include "arena.h"
#include "string.h"

// Overrides are copied into s_arena and live until app_identity_reset.

#define DEFAULT_DATA_DIR_NAME FL_S_(".flowi")
#define DEFAULT_LOG_FILE_NAME FL_S_("flowi.log")
#define DEFAULT_FILE_WATCHER_QUEUE FL_S_("org.flowi.filewatcher")

static FlArena* s_arena = nullptr;
static FlString s_data_dir_name = DEFAULT_DATA_DIR_NAME;
static FlString s_log_file_name = DEFAULT_LOG_FILE_NAME;
static FlString s_file_watcher_queue = DEFAULT_FILE_WATCHER_QUEUE;

static void set_field(FlString* field, FlString value) {
    if (value.length == 0) {
        return;
    }
    if (s_arena == nullptr) {
        s_arena = arena_new();
    }
    *field = fl_string_copy(s_arena, value);
}

void app_identity_set(FlString data_dir_name, FlString log_file_name, FlString file_watcher_queue) {
    set_field(&s_data_dir_name, data_dir_name);
    set_field(&s_log_file_name, log_file_name);
    set_field(&s_file_watcher_queue, file_watcher_queue);
}

void app_identity_reset(void) {
    if (s_arena != nullptr) {
        fl_arena_rewind(s_arena);
    }
    s_data_dir_name = (FlString) DEFAULT_DATA_DIR_NAME;
    s_log_file_name = (FlString) DEFAULT_LOG_FILE_NAME;
    s_file_watcher_queue = (FlString) DEFAULT_FILE_WATCHER_QUEUE;
}

FlString app_identity_data_dir_name(void) {
    return s_data_dir_name;
}

FlString app_identity_log_file_name(void) {
    return s_log_file_name;
}

FlString app_identity_file_watcher_queue(void) {
    return s_file_watcher_queue;
}
