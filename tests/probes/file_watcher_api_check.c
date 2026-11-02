///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compile-only parity guard: the api_gen-generated public file-watcher surface (<flowi/core/file_watcher.h>).
// Nothing is linked; every prototype must parse and match its expected signature here.

#include <stdbool.h>
#include <stdint.h>

#include <flowi/core/file_watcher.h>

// The change kinds are a bitmask: each must stay a distinct single bit so they can be OR-ed in change_types.
_Static_assert(FlFileChangeType_None == 0, "FlFileChangeType_None discriminant stable");
_Static_assert(FlFileChangeType_Created == 1, "FlFileChangeType_Created discriminant stable");
_Static_assert(FlFileChangeType_Modified == 2, "FlFileChangeType_Modified discriminant stable");
_Static_assert(FlFileChangeType_Deleted == 4, "FlFileChangeType_Deleted discriminant stable");
_Static_assert(FlFileChangeType_Renamed == 8, "FlFileChangeType_Renamed discriminant stable");
_Static_assert(FlFileChangeType_Moved == 16, "FlFileChangeType_Moved discriminant stable");

#define FL_FILE_WATCHER_FIELD_MATCHES(type, field, expected) \
    _Static_assert(__builtin_types_compatible_p(__typeof__(((type*)0)->field), expected), #type "::" #field " drift")

FL_FILE_WATCHER_FIELD_MATCHES(FlFileWatcherConfig, recursive, bool);
FL_FILE_WATCHER_FIELD_MATCHES(FlFileWatcherConfig, watch_files, bool);
FL_FILE_WATCHER_FIELD_MATCHES(FlFileWatcherConfig, watch_directories, bool);
FL_FILE_WATCHER_FIELD_MATCHES(FlFileWatcherConfig, max_changes, uint32_t);
FL_FILE_WATCHER_FIELD_MATCHES(FlFileChange, path, FlString);
FL_FILE_WATCHER_FIELD_MATCHES(FlFileChange, old_path, FlString);
FL_FILE_WATCHER_FIELD_MATCHES(FlFileChange, change_types, FlFileChangeType);
FL_FILE_WATCHER_FIELD_MATCHES(FlFileChange, timestamp, uint64_t);
FL_FILE_WATCHER_FIELD_MATCHES(FlFileChange, is_directory, bool);
FL_FILE_WATCHER_FIELD_MATCHES(FlFileChanges, changes, const FlFileChange*);
FL_FILE_WATCHER_FIELD_MATCHES(FlFileChanges, count, uint32_t);
FL_FILE_WATCHER_FIELD_MATCHES(FlFileChanges, has_overflow, bool);

#undef FL_FILE_WATCHER_FIELD_MATCHES

#define FL_FILE_WATCHER_FN_MATCHES(fn, ...) \
    _Static_assert(__builtin_types_compatible_p(__typeof__(fn), __VA_ARGS__), #fn " signature drift")

FL_FILE_WATCHER_FN_MATCHES(fl_file_watcher_start, uint32_t(FlString, const FlFileWatcherConfig*));
FL_FILE_WATCHER_FN_MATCHES(fl_file_watcher_stop, void(uint32_t));
FL_FILE_WATCHER_FN_MATCHES(fl_file_watcher_is_active, bool(uint32_t));
FL_FILE_WATCHER_FN_MATCHES(fl_file_watcher_has_changes, bool(uint32_t));
FL_FILE_WATCHER_FN_MATCHES(fl_file_watcher_take_changes, FlFileChanges(uint32_t, FlArena*));

#undef FL_FILE_WATCHER_FN_MATCHES
