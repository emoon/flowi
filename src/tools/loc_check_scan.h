#pragma once

#include "loc_check.h"

struct FlArena;

// One "SID_... literal the scanner found, with where it found it.
typedef struct SidRef {
    struct SidRef* next;
    FlString key;
    FlString path;
    u32 line;
} SidRef;

// Appends every "SID_... literal in source to the list whose tail link is *tail, and advances *tail. Grep-level: a
// runtime-assembled SID is not seen. The bare "SID_" prefix constant is skipped.

void loc_check_scan_source(struct FlArena* arena, const LocCheckSource* source, SidRef*** tail);
