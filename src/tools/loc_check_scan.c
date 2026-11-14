#include "loc_check_scan.h"
#include <core/arena.h>

#include <string.h>

// True when a "SID_ literal opens at text[i].

static bool starts_sid_literal(FlString text, u64 i) {
    if (text.data[i] != '"') {
        return false;
    }
    if (i + 5 > text.length) {
        return false;
    }
    return memcmp(text.data + i + 1, "SID_", 4) == 0;
}

// Finds the closing quote of the "SID_ literal opening at `start`. False when the literal is only the bare prefix or
// does not close on its line.

static bool ends_literal(char c) {
    return c == '"' || c == '\n';
}

static bool find_sid_literal_end(FlString text, u64 start, u64* out_end) {
    const u64 body = start + 5;
    u64 end = body;

    while (end < text.length && !ends_literal(text.data[end])) {
        end++;
    }
    if (end == body || end >= text.length) {
        return false;
    }
    if (text.data[end] != '"') {
        return false;
    }
    *out_end = end;
    return true;
}

static void scan_source(FlArena* arena, const LocCheckSource* source, SidRef*** tail) {
    const FlString text = source->text;
    u32 line = 1;

    for (u64 i = 0; i < text.length; i++) {
        if (text.data[i] == '\n') {
            line++;
            continue;
        }
        if (!starts_sid_literal(text, i)) {
            continue;
        }

        u64 end = 0;
        if (!find_sid_literal_end(text, i, &end)) {
            continue;
        }

        SidRef* ref = arena_alloc_zero(arena, SidRef);
        ref->key = fl_string_substr(text, i + 1, end - i - 1);
        ref->path = source->path;
        ref->line = line;
        **tail = ref;
        *tail = &ref->next;

        i = end;
    }
}

SidRef* loc_check_scan_sources(FlArena* arena, const LocCheckSource* sources, u32 source_count) {
    SidRef* refs = nullptr;
    SidRef** tail = &refs;

    for (u32 i = 0; i < source_count; i++) {
        scan_source(arena, &sources[i], &tail);
    }
    return refs;
}
