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

// Every "SID_... literal in sources, in source order, as a linked list allocated in arena. Grep-level: a
// runtime-assembled SID is not seen. The bare "SID_" prefix constant is skipped.

SidRef* loc_check_scan_sources(struct FlArena* arena, const LocCheckSource* sources, u32 source_count);
