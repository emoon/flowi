#pragma once

#include "core.h"
#include "string.h"

#include <flowi/path/path.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cross-platform path manipulation utilities

StringArray path_split(struct FlArena* arena, FlString path);

// Part of the filename before the first dot, or empty string if there is no dot or the filename
// starts with one
FlString path_get_filename_prefix(FlString path);

static inline bool path_is_separator(char c) {
    return c == '/' || c == '\\';
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Path components structure and functions for resolving a path through nested containers

typedef struct {
    StringArray segments;
    bool is_absolute;        // Whether the original path was absolute
    bool has_trailing_slash; // Whether the original path ended with a slash (for URLs)
} PathComponents;

PathComponents path_components(struct FlArena* arena, FlString path);

// Reconstruct a path from count segments starting at index start
FlString path_reconstruct_from_segments(const PathComponents* components, int start, u32 count, struct FlArena* arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Directory searching and current directory utilities

FlString path_get_current_directory(struct FlArena* arena);

// Search the current directory and then each ancestor for a directory named start_directory.
// Returns its full path, or an empty string if not found.
FlString path_find_directory(struct FlArena* arena, FlString start_directory);
