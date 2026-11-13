#include "path.h"
#include "arena.h"
#include "string.h"
#include "memory.h"
#include "os/os.h"
#include <ctype.h>

// Small classifiers and scans shared by the functions below

static bool is_http_url(FlString path) {
    return fl_string_begins_with(path, FL_S("http://")) || fl_string_begins_with(path, FL_S("https://"));
}

static bool is_drive_letter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// Windows drive root: "C:\" or "C:/"
static bool has_drive_root(FlString path) {
    return path.length >= 3 && is_drive_letter(path.data[0]) && path.data[1] == ':' && path_is_separator(path.data[2]);
}

// Windows UNC path: "\\server\share" or "//server/share"
static bool is_unc(FlString path) {
    return path.length >= 2 && path_is_separator(path.data[0]) && path_is_separator(path.data[1]);
}

static bool has_trailing_separator(FlString path) {
    return path.length > 0 && path_is_separator(path.data[path.length - 1]);
}

// Index of the last separator within the first end bytes of path, or -1
static i64 last_separator_index(FlString path, u64 end) {
    for (i64 i = (i64)end - 1; i >= 0; i--) {
        if (path_is_separator(path.data[i])) {
            return i;
        }
    }
    return -1;
}

// Index of the last c in str, or -1
static i64 last_char_index(FlString str, char c) {
    for (i64 i = (i64)str.length - 1; i >= 0; i--) {
        if (str.data[i] == c) {
            return i;
        }
    }
    return -1;
}

static FlString copy_prefix(struct FlArena* arena, FlString path, u64 length) {
    return fl_string_copy(arena, (FlString) { .data = path.data, .length = length });
}

static FlString copy_root(struct FlArena* arena) {
    return fl_string_copy(arena, (FlString) { .data = "/", .length = 1 });
}

// Normalization

// End of the "scheme://" part of a URL, whose double slash has to survive normalization. 0 when there is none.
static u64 url_scheme_end(FlString url) {
    for (u64 i = 1; i + 1 < url.length; i++) {
        if (url.data[i] == '/' && url.data[i + 1] == '/') {
            return i + 2;
        }
    }
    return 0;
}

// Write path to buffer with backslashes turned into slashes and runs of slashes collapsed to one. Slashes before
// keep_until are kept as they are. Returns the number of bytes written.
static u64 collapse_separators(FlString path, u64 keep_until, char* buffer) {
    u64 out_len = 0;
    bool prev_slash = false;

    for (u64 i = 0; i < path.length; i++) {
        const char c = path.data[i] == '\\' ? '/' : path.data[i];
        const bool is_slash = c == '/';
        const bool is_duplicate = is_slash && prev_slash && i >= keep_until;

        if (!is_duplicate) {
            buffer[out_len++] = c;
        }
        prev_slash = is_slash;
    }

    return out_len;
}

FlString fl_path_normalize(struct FlArena* arena, FlString path) {
    if (path.length == 0) {
        return (FlString) { 0 };
    }

    const bool is_url = is_http_url(path);
    const u64 keep_until = is_url ? url_scheme_end(path) : 0;

    char* buffer = arena_alloc_array(arena, char, path.length + 1);
    u64 out_len = collapse_separators(path, keep_until, buffer);

    // A trailing slash is kept on the root and on URLs, where it indicates a directory
    const bool drops_trailing_slash = !is_url && out_len > 1 && buffer[out_len - 1] == '/';
    if (drops_trailing_slash) {
        out_len--;
    }

    return (FlString) { .data = buffer, .length = out_len, .is_ascii = path.is_ascii };
}

static bool can_pop_segment(const FlString* resolved, u32 count) {
    return count > 0 && !fl_string_equals(resolved[count - 1], FL_S(".."));
}

// Resolve the "." and ".." segments into resolved, which has room for every segment. Returns the number kept.
static u32 resolve_dot_segments(StringArray segments, bool is_absolute, FlString* resolved) {
    u32 count = 0;

    for_count(i, (u32)segments.count) {
        const FlString seg = segments.items[i];
        if (fl_string_equals(seg, FL_S("."))) {
            continue;
        }

        const bool is_parent = fl_string_equals(seg, FL_S(".."));
        if (is_parent && can_pop_segment(resolved, count)) {
            count--;
        } else if (!is_parent || !is_absolute) {
            // Nothing to pop on a relative path: keep the ".." so the result
            // still points above its (unknown) base. On an absolute path the
            // root has no parent, so an un-poppable ".." is dropped.
            resolved[count++] = seg;
        }
    }

    return count;
}

FlString fl_path_normalize_full(struct FlArena* arena, FlString path) {
    // Absolute-ness must be recorded up front: path_split drops the leading empty
    // segment, so nothing downstream can recover it.
    const bool is_absolute = path.length > 0 && path.data[0] == '/';

    const StringArray segments = path_split(arena, path);
    FlString* resolved = arena_alloc_array(arena, FlString, segments.count);
    const u32 resolved_count = resolve_dot_segments(segments, is_absolute, resolved);

    if (resolved_count == 0) {
        return is_absolute ? FL_S("/") : FL_S(".");
    }

    FlString result = is_absolute ? FL_S("/") : fl_string_copy(arena, resolved[0]);
    for (u32 i = is_absolute ? 0 : 1; i < resolved_count; i++) {
        result = fl_path_join(arena, result, resolved[i]);
    }

    return result;
}

// Splitting and joining

static u32 count_separators(FlString path) {
    u32 count = 0;
    for (u64 i = 0; i < path.length; i++) {
        if (path_is_separator(path.data[i])) {
            count++;
        }
    }
    return count;
}

StringArray path_split(struct FlArena* arena, FlString path) {
    if (path.length == 0) {
        return (StringArray) { .items = nullptr, .count = 0, .capacity = 0 };
    }

    const u32 capacity = count_separators(path) + 1;
    FlString* segments = arena_alloc_array_zero(arena, FlString, capacity);
    u32 segment_count = 0;
    u64 start = 0;

    for (u64 i = 0; i <= path.length; i++) {
        const bool is_segment_end = i == path.length || path_is_separator(path.data[i]);
        if (!is_segment_end) {
            continue;
        }

        // Empty segments (leading, trailing and doubled separators) are dropped
        if (i > start) {
            segments[segment_count++] = fl_string_substr(path, start, i - start);
        }
        start = i + 1;
    }

    return (StringArray) { .items = segments, .count = segment_count, .capacity = capacity };
}

// An absolute relative part replaces the base, with one exception: a single leading slash on a single component
// ("/name") counts as a separator rather than making it absolute.
static bool replaces_base(FlString relative) {
    if (!fl_path_is_absolute(relative)) {
        return false;
    }

    const FlString after_first = fl_string_substr(relative, 1, relative.length - 1);
    const bool is_single_component = relative.data[0] == '/' && !fl_path_has_separator(after_first);
    return !is_single_component;
}

// Concatenate two non-empty parts with exactly one separator between them: one is added when neither side brings
// it, and one is skipped when both do.
static FlString join_parts(struct FlArena* arena, FlString base, FlString relative) {
    const bool base_has_separator = path_is_separator(base.data[base.length - 1]);
    const bool relative_has_separator = path_is_separator(relative.data[0]);
    const bool adds_separator = !base_has_separator && !relative_has_separator;
    const u64 skip = (base_has_separator && relative_has_separator) ? 1 : 0;

    const u64 total_len = base.length + (adds_separator ? 1 : 0) + relative.length - skip;
    char* buffer = arena_alloc_array(arena, char, total_len + 1);

    u64 offset = base.length;
    memory_copy(buffer, total_len + 1, base.data, base.length);
    if (adds_separator) {
        buffer[offset++] = '/';
    }
    memory_copy(buffer + offset, total_len + 1 - offset, relative.data + skip, relative.length - skip);

    return (FlString) { .data = buffer, .length = total_len, .is_ascii = base.is_ascii && relative.is_ascii };
}

FlString fl_path_join(struct FlArena* arena, FlString base, FlString relative) {
    if (base.length == 0) {
        return fl_string_copy(arena, relative);
    }
    if (relative.length == 0) {
        return fl_string_copy(arena, base);
    }
    if (replaces_base(relative)) {
        return fl_string_copy(arena, relative);
    }

    return join_parts(arena, base, relative);
}

// Parts of a path

FlString fl_path_get_filename(FlString path) {
    const u64 start = (u64)(last_separator_index(path, path.length) + 1);
    return fl_string_substr(path, start, path.length - start);
}

FlString fl_path_get_extension(FlString path) {
    const FlString filename = fl_path_get_filename(path);
    const i64 last_dot = last_char_index(filename, '.');

    // A leading dot is not an extension (hidden files), and a trailing dot has none after it
    if (last_dot <= 0) {
        return (FlString) { 0 };
    }

    const u64 start = (u64)last_dot + 1;
    return fl_string_substr(filename, start, filename.length - start);
}

FlString path_get_filename_prefix(FlString path) {
    const FlString filename = fl_path_get_filename(path);

    // Don't count leading dot as prefix separator (hidden files)
    const i64 first_dot = fl_string_find_char_from(filename, '.', 1);
    if (first_dot < 0) {
        return (FlString) { 0 };
    }

    return fl_string_substr(filename, 0, (u64)first_dot);
}

FlString fl_path_get_basename(struct FlArena* arena, FlString path) {
    const FlString filename = fl_path_get_filename(path);
    const i64 last_dot = last_char_index(filename, '.');

    // A leading dot belongs to the name (hidden files)
    return last_dot > 0 ? copy_prefix(arena, filename, (u64)last_dot) : fl_string_copy(arena, filename);
}

// The part before the last separator within the first end bytes of path: "/" when that separator is the leading
// one, and empty when there is none.
static FlString directory_part(struct FlArena* arena, FlString path, u64 end) {
    const i64 last_sep = last_separator_index(path, end);
    if (last_sep < 0) {
        return (FlString) { 0 };
    }

    return last_sep == 0 ? copy_root(arena) : copy_prefix(arena, path, (u64)last_sep);
}

FlString fl_path_get_directory(struct FlArena* arena, FlString path) {
    return directory_part(arena, path, path.length);
}

FlString fl_path_get_parent(struct FlArena* arena, FlString path) {
    // Trailing separators do not start a new component
    u64 end = path.length;
    while (end > 1 && path_is_separator(path.data[end - 1])) {
        end--;
    }

    return directory_part(arena, path, end);
}

// Queries

bool fl_path_is_absolute(FlString path) {
    if (path.length == 0) {
        return false;
    }

    // Unix absolute path
    if (path.data[0] == '/') {
        return true;
    }

    // HTTP/HTTPS URLs are considered absolute
    return has_drive_root(path) || is_unc(path) || is_http_url(path);
}

bool fl_path_is_relative(FlString path) {
    return !fl_path_is_absolute(path);
}

FlString fl_path_make_absolute(struct FlArena* arena, FlString base, FlString path) {
    if (fl_path_is_absolute(path)) {
        return fl_string_copy(arena, path);
    }

    return fl_path_join(arena, base, path);
}

// The form of c that fl_path_equals compares: both separators are the same, and on Windows so are both cases
static int path_equals_char(char c) {
    if (path_is_separator(c)) {
        return '/';
    }
#ifdef _WIN32
    return tolower(c);
#else
    return c;
#endif
}

bool fl_path_equals(FlString path1, FlString path2) {
    if (path1.length != path2.length) {
        return false;
    }

    for (u64 i = 0; i < path1.length; i++) {
        if (path_equals_char(path1.data[i]) != path_equals_char(path2.data[i])) {
            return false;
        }
    }
    return true;
}

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

bool fl_path_has_separator(FlString path) {
    return last_separator_index(path, path.length) >= 0;
}

// Separator edits

FlString fl_path_remove_trailing_separator(struct FlArena* arena, FlString path) {
    if (path.length == 0) {
        return (FlString) { 0 };
    }

    // Don't remove separator from root path
    const bool removes_separator = path.length > 1 && has_trailing_separator(path);
    return removes_separator ? copy_prefix(arena, path, path.length - 1) : fl_string_copy(arena, path);
}

FlString fl_path_ensure_trailing_separator(struct FlArena* arena, FlString path) {
    if (path.length == 0) {
        return copy_root(arena);
    }

    if (has_trailing_separator(path)) {
        return fl_string_copy(arena, path);
    }

    char* buffer = arena_alloc_array(arena, char, path.length + 2);
    memory_copy(buffer, path.length + 2, path.data, path.length);
    buffer[path.length] = '/';

    return (FlString) { .data = buffer, .length = path.length + 1, .is_ascii = path.is_ascii };
}

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

// Path components functions for resolving a path through nested containers

PathComponents path_components(struct FlArena* arena, FlString path) {
    const FlString normalized = fl_path_normalize(arena, path);

    return (PathComponents) {
        .segments = path_split(arena, normalized),
        .is_absolute = fl_path_is_absolute(path),
        // Taken before normalization, which drops it (important for URLs indicating directories)
        .has_trailing_slash = has_trailing_separator(path),
    };
}

static bool is_url_scheme_segment(FlString segment) {
    return fl_string_equals(segment, FL_S("https:")) || fl_string_equals(segment, FL_S("http:"));
}

// A single letter followed by a colon, e.g. "C:"
static bool is_drive_segment(FlString segment) {
    return segment.length == 2 && segment.data[1] == ':' && is_drive_letter(segment.data[0]);
}

// Whether the range of count segments from start begins a URL, which needs at least two segments (scheme + domain)
static bool range_is_url(const PathComponents* components, int start, u32 count) {
    const bool has_scheme_and_domain = (int)count > start + 1 && components->segments.count > start;
    return has_scheme_and_domain && is_url_scheme_segment(components->segments.items[start]);
}

// Whether the range from start begins with the drive of a Windows absolute path
static bool range_is_windows_absolute(const PathComponents* components, int start) {
    const bool has_first_segment = components->is_absolute && components->segments.count > start;
    return has_first_segment && is_drive_segment(components->segments.items[start]);
}

// "scheme://domain" from the first two segments of a URL: the scheme takes "//" after it and the domain follows
// without a separator
static FlString url_origin(struct FlArena* arena, const FlString* segments) {
    const FlString scheme = fl_string_concat(arena, segments[0], FL_S("//"));
    return fl_string_concat(arena, scheme, segments[1]);
}

FlString path_reconstruct_from_segments(const PathComponents* components, const int start, const u32 count,
                                        struct FlArena* arena) {
    const FlString* segments = components->segments.items;
    const bool is_url = range_is_url(components, start, count);

    // Windows absolute paths don't need a leading slash
    const bool has_leading_slash = components->is_absolute && !range_is_windows_absolute(components, start);

    FlString result = has_leading_slash ? FL_S("/") : FL_S("");
    u32 next = (u32)start;
    if (is_url) {
        result = url_origin(arena, segments + start);
        next += 2;
    }

    for (u32 i = next; i < count; i++) {
        result = fl_path_join(arena, result, segments[i]);
    }

    // On a URL a trailing slash indicates a directory, so preserve it when the range runs to the last segment
    const bool keeps_trailing_slash = components->has_trailing_slash && count == (u32)components->segments.count;
    if (is_url && keeps_trailing_slash) {
        result = fl_string_concat(arena, result, FL_S("/"));
    }

    return result;
}

// Directory searching and current directory utilities

FlString path_get_current_directory(struct FlArena* arena) {
    return get_current_directory_os(arena);
}

// Look for name in each ancestor of directory, nearest first. Returns its path, or an empty string.
static FlString find_in_ancestors(struct FlArena* arena, FlString directory, FlString name) {
    const PathComponents components = path_components(arena, directory);
    const u32 segment_count = (u32)components.segments.count;

    for (u32 levels = 1; levels < segment_count; levels++) {
        const FlString parent = path_reconstruct_from_segments(&components, 0, segment_count - levels, arena);
        const FlString candidate = fl_path_join(arena, parent, name);

        if (file_directory_exists_os(candidate)) {
            return candidate;
        }
    }

    return (FlString) { 0 };
}

// Root of the filesystem an absolute directory is on: "/", or a drive root such as "C:\". Empty when directory is
// relative or in an unknown absolute form.
static FlString filesystem_root(FlString directory) {
    if (!fl_path_is_absolute(directory)) {
        return (FlString) { 0 };
    }

    if (fl_string_begins_with(directory, FL_S("/"))) {
        return FL_S("/");
    }

    const bool has_drive = directory.length >= 3 && directory.data[1] == ':';
    return has_drive ? fl_string_substr(directory, 0, 3) : (FlString) { 0 };
}

FlString path_find_directory(struct FlArena* arena, FlString start_directory) {
    if (start_directory.length == 0) {
        return (FlString) { 0 };
    }

    if (file_directory_exists_os(start_directory)) {
        return fl_string_copy(arena, start_directory);
    }

    const FlString current_dir = path_get_current_directory(arena);
    if (current_dir.length == 0) {
        return (FlString) { 0 };
    }

    const FlString in_ancestor = find_in_ancestors(arena, current_dir, start_directory);
    if (in_ancestor.length > 0) {
        return in_ancestor;
    }

    // Also try from filesystem root as last resort
    const FlString root = filesystem_root(current_dir);
    if (root.length == 0) {
        return (FlString) { 0 };
    }

    const FlString from_root = fl_path_join(arena, root, start_directory);
    return file_directory_exists_os(from_root) ? from_root : (FlString) { 0 };
}
