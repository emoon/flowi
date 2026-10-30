#include "path.h"
#include "arena.h"
#include "string.h"
#include "log.h"
#include "memory.h"
#include "os/os.h"
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_path_normalize(struct FlArena* arena, FlString path) {
    if (path.length == 0) {
        return (FlString) { 0 };
    }

    bool is_url = fl_string_begins_with(path, FL_S("http://")) || fl_string_begins_with(path, FL_S("https://"));

    char* buffer = arena_alloc_array(arena, char, path.length + 1);
    u64 out_len = 0;

    bool prev_slash = false;
    u64 protocol_end = 0;

    if (is_url) {
        for (u64 i = 0; i < path.length - 1; i++) {
            if (path.data[i] == '/' && path.data[i + 1] == '/' && i > 0) {
                protocol_end = i + 2; // Include both slashes in protocol part
                break;
            }
        }
    }

    for (u64 i = 0; i < path.length; i++) {
        char c = path.data[i];

        if (c == '\\') {
            c = '/';
        }

        // Skip duplicate slashes, but preserve them in URL protocol part
        if (c == '/') {
            if (prev_slash && i >= protocol_end) {
                continue;
            }
            prev_slash = true;
        } else {
            prev_slash = false;
        }

        buffer[out_len++] = c;
    }

    // A trailing slash is kept on the root and on URLs, where it indicates a directory
    if (out_len > 1 && buffer[out_len - 1] == '/' && !is_url) {
        out_len--;
    }

    return (FlString) { .data = buffer, .length = out_len, .is_ascii = path.is_ascii };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_path_normalize_full(struct FlArena* arena, FlString path) {
    // Absolute-ness must be recorded up front: path_split drops the leading empty
    // segment, so nothing downstream can recover it.
    bool is_absolute = path.length > 0 && path.data[0] == '/';

    StringArray segments = path_split(arena, path);

    FlString* resolved = arena_alloc_array(arena, FlString, segments.count);
    u32 resolved_count = 0;

    for_count(i, (u32)segments.count) {
        FlString seg = segments.items[i];

        if (fl_string_equals(seg, FL_S("."))) {
            continue;
        } else if (fl_string_equals(seg, FL_S(".."))) {
            if (resolved_count > 0 && !fl_string_equals(resolved[resolved_count - 1], FL_S(".."))) {
                resolved_count--;
            } else if (!is_absolute) {
                // Nothing to pop on a relative path: keep the ".." so the result
                // still points above its (unknown) base. On an absolute path the
                // root has no parent, so an un-poppable ".." is dropped.
                resolved[resolved_count++] = seg;
            }
        } else {
            resolved[resolved_count++] = seg;
        }
    }

    if (resolved_count == 0) {
        return is_absolute ? FL_S("/") : FL_S(".");
    }

    FlString result;
    u32 first = 0;
    if (is_absolute) {
        result = FL_S("/");
    } else {
        result = fl_string_copy(arena, resolved[0]);
        first = 1;
    }
    for (u32 i = first; i < resolved_count; i++) {
        result = fl_path_join(arena, result, resolved[i]);
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StringArray path_split(struct FlArena* arena, FlString path) {
    if (path.length == 0) {
        return (StringArray) { .items = nullptr, .count = 0, .capacity = 0 };
    }

    u32 count = 1;
    for (u64 i = 0; i < path.length; i++) {
        if (path_is_separator(path.data[i])) {
            count++;
        }
    }

    FlString* segments = arena_alloc_array_zero(arena, FlString, count);
    u32 segment_idx = 0;
    u64 start = 0;

    for (u64 i = 0; i <= path.length; i++) {
        if (i == path.length || path_is_separator(path.data[i])) {
            if (i > start) {
                segments[segment_idx].data = path.data + start;
                segments[segment_idx].length = i - start;
                segments[segment_idx].is_ascii = path.is_ascii;
                segment_idx++;
            }
            start = i + 1;
        }
    }

    return (StringArray) { .items = segments, .count = segment_idx, .capacity = count };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_path_join(struct FlArena* arena, FlString base, FlString relative) {
    if (base.length == 0) {
        return fl_string_copy(arena, relative);
    }
    if (relative.length == 0) {
        return fl_string_copy(arena, base);
    }

    // A single leading slash on the relative component counts as a separator rather than making it
    // absolute, unless it holds further separators
    if (fl_path_is_absolute(relative)) {
        if (relative.length > 0 && relative.data[0] == '/'
            && !(relative.length >= 2 && relative.data[1] == '/')) { // Not UNC
            bool has_more_separators = false;
            for (u64 i = 1; i < relative.length; i++) {
                if (path_is_separator(relative.data[i])) {
                    has_more_separators = true;
                    break;
                }
            }
            if (!has_more_separators) {
                // Single component with leading slash - treat as relative separator
            } else {
                return fl_string_copy(arena, relative);
            }
        } else {
            return fl_string_copy(arena, relative);
        }
    }

    bool base_has_separator = base.length > 0 && path_is_separator(base.data[base.length - 1]);
    bool relative_has_separator = relative.length > 0 && path_is_separator(relative.data[0]);

    u64 total_len = base.length + relative.length;
    if (!base_has_separator && !relative_has_separator) {
        total_len++;
    } else if (base_has_separator && relative_has_separator) {
        total_len--;
    }

    char* buffer = arena_alloc_array(arena, char, total_len + 1);
    memory_copy(buffer, total_len + 1, base.data, base.length);

    u64 offset = base.length;
    const char* relative_start = relative.data;
    u64 relative_len = relative.length;

    if (!base_has_separator && !relative_has_separator) {
        buffer[offset++] = '/';
    } else if (base_has_separator && relative_has_separator) {
        relative_start++;
        relative_len--;
    }

    memory_copy(buffer + offset, total_len + 1 - offset, relative_start, relative_len);

    return (FlString) { .data = buffer, .length = total_len, .is_ascii = base.is_ascii && relative.is_ascii };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_path_get_extension(FlString path) {
    if (path.length == 0) {
        return (FlString) { 0 };
    }

    i64 last_dot = -1;
    i64 last_sep = -1;

    for (i64 i = path.length - 1; i >= 0; i--) {
        if (path.data[i] == '.' && last_dot == -1) {
            last_dot = i;
        } else if (path_is_separator(path.data[i])) {
            last_sep = i;
            break;
        }
    }

    // A leading dot is not an extension (hidden files)
    if (last_dot > last_sep && last_dot < (i64)path.length - 1 && last_dot != last_sep + 1) {
        return (FlString) { .data = path.data + last_dot + 1,
                            .length = path.length - last_dot - 1,
                            .is_ascii = path.is_ascii };
    }

    return (FlString) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString path_get_filename_prefix(FlString path) {
    if (path.length == 0) {
        return (FlString) { 0 };
    }

    FlString filename = fl_path_get_filename(path);
    if (filename.length == 0) {
        return (FlString) { 0 };
    }

    for (u64 i = 0; i < filename.length; i++) {
        if (filename.data[i] == '.') {
            // Don't count leading dot as prefix separator (hidden files)
            if (i == 0) {
                continue;
            }
            return (FlString) { .data = filename.data, .length = i, .is_ascii = filename.is_ascii };
        }
    }

    return (FlString) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_path_get_filename(FlString path) {
    if (path.length == 0) {
        return (FlString) { 0 };
    }

    i64 last_sep = -1;
    for (i64 i = path.length - 1; i >= 0; i--) {
        if (path_is_separator(path.data[i])) {
            last_sep = i;
            break;
        }
    }

    if (last_sep == (i64)path.length - 1) {
        return (FlString) { 0 };
    }

    return (
        FlString) { .data = path.data + last_sep + 1, .length = path.length - last_sep - 1, .is_ascii = path.is_ascii };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_path_get_directory(struct FlArena* arena, FlString path) {
    if (path.length == 0) {
        return (FlString) { 0 };
    }

    i64 last_sep = -1;
    for (i64 i = path.length - 1; i >= 0; i--) {
        if (path_is_separator(path.data[i])) {
            last_sep = i;
            break;
        }
    }

    if (last_sep <= 0) {
        // No directory part, or it's root
        if (last_sep == 0) {
            return fl_string_copy(arena, (FlString) { .data = "/", .length = 1 });
        }
        return (FlString) { 0 };
    }

    return fl_string_copy(arena, (FlString) { .data = path.data, .length = (u64)last_sep });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_path_get_basename(struct FlArena* arena, FlString path) {
    FlString filename = fl_path_get_filename(path);
    if (filename.length == 0) {
        return (FlString) { 0 };
    }

    i64 last_dot = -1;
    for (i64 i = filename.length - 1; i >= 0; i--) {
        if (filename.data[i] == '.') {
            last_dot = i;
            break;
        }
    }

    if (last_dot > 0) {
        return fl_string_copy(arena, (FlString) { .data = filename.data, .length = (u64)last_dot });
    }

    return fl_string_copy(arena, filename);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_path_is_absolute(FlString path) {
    if (path.length == 0) {
        return false;
    }

    // Unix absolute path
    if (path.data[0] == '/') {
        return true;
    }

    // Windows absolute path (C:\ or C:/)
    if (path.length >= 3 && isalpha(path.data[0]) && path.data[1] == ':' && path_is_separator(path.data[2])) {
        return true;
    }

    // Windows UNC path (\\server\share or //server/share)
    if (path.length >= 2 && path_is_separator(path.data[0]) && path_is_separator(path.data[1])) {
        return true;
    }

    // HTTP/HTTPS URLs are considered absolute
    if (path.length >= 7) {
        if (fl_string_begins_with(path, FL_S("http://"))
            || (path.length >= 8 && fl_string_begins_with(path, FL_S("https://")))) {
            return true;
        }
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_path_is_relative(FlString path) {
    return !fl_path_is_absolute(path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_path_make_absolute(struct FlArena* arena, FlString base, FlString path) {
    if (fl_path_is_absolute(path)) {
        return fl_string_copy(arena, path);
    }

    return fl_path_join(arena, base, path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_path_equals(FlString path1, FlString path2) {
    if (path1.length == 0 && path2.length == 0) {
        return true;
    }
    if (path1.length == 0 || path2.length == 0) {
        return false;
    }

    if (path1.length != path2.length) {
        return false;
    }

#ifdef _WIN32
    for (u64 i = 0; i < path1.length; i++) {
        char c1 = path1.data[i];
        char c2 = path2.data[i];

        if (path_is_separator(c1))
            c1 = '/';
        if (path_is_separator(c2))
            c2 = '/';

        if (tolower(c1) != tolower(c2)) {
            return false;
        }
    }
    return true;
#else
    for (u64 i = 0; i < path1.length; i++) {
        char c1 = path1.data[i];
        char c2 = path2.data[i];

        if (path_is_separator(c1))
            c1 = '/';
        if (path_is_separator(c2))
            c2 = '/';

        if (c1 != c2) {
            return false;
        }
    }
    return true;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_path_has_extension(FlString path, FlString extension) {
    FlString ext = fl_path_get_extension(path);
    if (ext.length == 0 || extension.length == 0) {
        return false;
    }

    if (ext.length != extension.length) {
        return false;
    }

    for (u64 i = 0; i < ext.length; i++) {
        if (tolower(ext.data[i]) != tolower(extension.data[i])) {
            return false;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_path_remove_trailing_separator(struct FlArena* arena, FlString path) {
    if (path.length == 0) {
        return (FlString) { 0 };
    }

    // Don't remove separator from root path
    if (path.length == 1 && path_is_separator(path.data[0])) {
        return fl_string_copy(arena, path);
    }

    if (path_is_separator(path.data[path.length - 1])) {
        return fl_string_copy(arena, (FlString) { .data = path.data, .length = path.length - 1 });
    }

    return fl_string_copy(arena, path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_path_ensure_trailing_separator(struct FlArena* arena, FlString path) {
    if (path.length == 0) {
        return fl_string_copy(arena, (FlString) { .data = "/", .length = 1 });
    }

    if (path_is_separator(path.data[path.length - 1])) {
        return fl_string_copy(arena, path);
    }

    char* buffer = arena_alloc_array(arena, char, path.length + 2);
    memory_copy(buffer, path.length + 2, path.data, path.length);
    buffer[path.length] = '/';

    return (FlString) { .data = buffer, .length = path.length + 1, .is_ascii = path.is_ascii };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_path_get_parent(struct FlArena* arena, FlString path) {
    if (path.length == 0) {
        return (FlString) { 0 };
    }

    u64 end = path.length;
    while (end > 1 && path_is_separator(path.data[end - 1])) {
        end--;
    }

    i64 last_sep = -1;
    for (i64 i = end - 1; i >= 0; i--) {
        if (path_is_separator(path.data[i])) {
            last_sep = i;
            break;
        }
    }

    if (last_sep <= 0) {
        // No parent or it's root
        if (last_sep == 0) {
            return fl_string_copy(arena, (FlString) { .data = "/", .length = 1 });
        }
        return (FlString) { 0 };
    }

    return fl_string_copy(arena, (FlString) { .data = path.data, .length = (u64)last_sep });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_path_has_separator(FlString path) {
    for (u64 i = 0; i < path.length; i++) {
        if (path_is_separator(path.data[i])) {
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_path_to_forward_slashes(struct FlArena* arena, FlString path) {
    if (path.length == 0) {
        return (FlString) { 0 };
    }

    char* buffer = arena_alloc_array(arena, char, path.length + 1);
    for (u64 i = 0; i < path.length; i++) {
        buffer[i] = path_is_separator(path.data[i]) ? '/' : path.data[i];
    }

    return (FlString) { .data = buffer, .length = path.length, .is_ascii = path.is_ascii };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Path components functions for resolving a path through nested containers

PathComponents path_components(struct FlArena* arena, FlString path) {
    // Check for trailing slash before normalization (important for URLs indicating directories)
    bool has_trailing_slash
        = (path.length > 0 && (path.data[path.length - 1] == '/' || path.data[path.length - 1] == '\\'));

    FlString normalized = fl_path_normalize(arena, path);
    StringArray segments = path_split(arena, normalized);
    bool is_absolute = fl_path_is_absolute(path);

    return (
        PathComponents) { .segments = segments, .is_absolute = is_absolute, .has_trailing_slash = has_trailing_slash };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString path_reconstruct_from_segments(const PathComponents* components, const int start, const u32 count,
                                        struct FlArena* arena) {
    // A URL needs at least two segments (scheme + domain)
    bool is_url = false;
    if ((int)count > start + 1 && (int)components->segments.count > start) {
        FlString first_segment = components->segments.items[start];
        is_url = fl_string_equals(first_segment, FL_S("https:")) || fl_string_equals(first_segment, FL_S("http:"));
    }

    // Windows absolute path: a first segment of a single letter followed by a colon, e.g. "C:"
    bool is_windows_absolute = false;
    if (components->is_absolute && (int)components->segments.count > start) {
        FlString first_segment = components->segments.items[start];
        is_windows_absolute = (first_segment.length == 2 && first_segment.data[1] == ':'
                               && ((first_segment.data[0] >= 'A' && first_segment.data[0] <= 'Z')
                                   || (first_segment.data[0] >= 'a' && first_segment.data[0] <= 'z')));
    }

    FlString result;
    if (is_url) {
        result = FL_S("");
    } else if (is_windows_absolute) {
        // Windows absolute paths don't need a leading slash
        result = FL_S("");
    } else {
        result = components->is_absolute ? FL_S("/") : FL_S("");
    }

    for (u32 i = (u32)start; i < count; i++) {
        if (is_url && (int)i == start) {
            // The first segment is the scheme; add it directly with //
            result = fl_string_concat(arena, components->segments.items[i], FL_S("//"));
        } else if (is_url && (int)i == start + 1) {
            // Join without a separator since :// was already added
            result = fl_string_concat(arena, result, components->segments.items[i]);
        } else {
            result = fl_path_join(arena, result, components->segments.items[i]);
        }
    }

    // On a URL a trailing slash indicates a directory, so preserve it
    if (is_url && components->has_trailing_slash && count == (u32)components->segments.count) {
        result = fl_string_concat(arena, result, FL_S("/"));
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Directory searching and current directory utilities

FlString path_get_current_directory(struct FlArena* arena) {
    return get_current_directory_os(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString path_find_directory(struct FlArena* arena, FlString start_directory) {
    if (start_directory.length == 0) {
        return (FlString) { 0 };
    }

    if (file_directory_exists_os(start_directory)) {
        return fl_string_copy(arena, start_directory);
    }

    FlString current_dir = path_get_current_directory(arena);
    if (current_dir.length == 0) {
        return (FlString) { 0 };
    }

    PathComponents current_components = path_components(arena, current_dir);

    // Walk backwards up the directory tree
    for (u32 levels = 1; levels < (u32)current_components.segments.count; levels++) {
        u32 parent_segment_count = (u32)current_components.segments.count - levels;
        FlString parent_path = path_reconstruct_from_segments(&current_components, 0, parent_segment_count, arena);

        FlString candidate_path = fl_path_join(arena, parent_path, start_directory);

        if (file_directory_exists_os(candidate_path)) {
            return candidate_path;
        }
    }

    // Also try from filesystem root as last resort
    FlString root_path;
    if (fl_path_is_absolute(current_dir)) {
        if (fl_string_begins_with(current_dir, FL_S("/"))) {
            root_path = fl_path_join(arena, FL_S("/"), start_directory);
        }
        // On Windows, try from drive root
        else if (current_dir.length >= 3 && current_dir.data[1] == ':') {
            FlString drive = { .data = current_dir.data, .length = 3 }; // e.g., "C:\"
            root_path = fl_path_join(arena, drive, start_directory);
        } else {
            return (FlString) { 0 }; // Unknown absolute path format
        }

        if (file_directory_exists_os(root_path)) {
            return root_path;
        }
    }

    return (FlString) { 0 };
}
