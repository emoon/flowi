#include "app_identity.h"
#include "string.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Zero-length means "not set". Overrides are malloc-copied and held until replaced or reset.

static FlString s_data_dir_name = { 0 };
static FlString s_log_file_name = { 0 };
static FlString s_file_watcher_queue = { 0 };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void set_field(FlString* field, FlString value) {
    if (value.length == 0) {
        return;
    }
    if (field->length > 0) {
        fl_string_free(*field);
    }
    *field = fl_string_copy_malloc(value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_app_identity_set(const FlAppIdentity* identity) {
    if (identity == nullptr) {
        return;
    }
    set_field(&s_data_dir_name, identity->data_dir_name);
    set_field(&s_log_file_name, identity->log_file_name);
    set_field(&s_file_watcher_queue, identity->file_watcher_queue);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void app_identity_reset(void) {
    if (s_data_dir_name.length > 0) {
        fl_string_free(s_data_dir_name);
    }
    if (s_log_file_name.length > 0) {
        fl_string_free(s_log_file_name);
    }
    if (s_file_watcher_queue.length > 0) {
        fl_string_free(s_file_watcher_queue);
    }
    s_data_dir_name = (FlString) { 0 };
    s_log_file_name = (FlString) { 0 };
    s_file_watcher_queue = (FlString) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_app_identity_data_dir_name(void) {
    return s_data_dir_name.length > 0 ? s_data_dir_name : FL_S(".flowi");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_app_identity_log_file_name(void) {
    return s_log_file_name.length > 0 ? s_log_file_name : FL_S("flowi.log");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_app_identity_file_watcher_queue(void) {
    return s_file_watcher_queue.length > 0 ? s_file_watcher_queue : FL_S("org.flowi.filewatcher");
}
