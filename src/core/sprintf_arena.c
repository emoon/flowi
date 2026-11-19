#include "sprintf.h"
#include "arena.h"
#include "string.h"
#include "memory.h"
#include "stb_sprintf.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct SprintfArenaContext {
    FlArena* arena;
    u64 total_len;
} SprintfArenaContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char* sprintf_arena_callback(const char* buf, void* user, int len) {
    SprintfArenaContext* ctx = (SprintfArenaContext*)user;

    char* dest = arena_alloc_array(ctx->arena, char, len);
    // Copy the formatted data from buf to arena
    memory_copy(dest, len, buf, len);

    ctx->total_len += len;

    // Return the same buffer for reuse
    return (char*)buf;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Assembles formatted chunks in scratch.
static FlString sprintf_assemble(FlArena* scratch, const char* format, va_list args) {
    SprintfArenaContext ctx = { .arena = scratch };

    const u64 start_pos = atomic_load_explicit(&scratch->pos, memory_order_relaxed);

    char temp_buffer[STB_SPRINTF_MIN];
    stbsp_vsprintfcb(sprintf_arena_callback, &ctx, temp_buffer, format, args);

    // Cast to char* for pointer arithmetic (MSVC doesn't support void* arithmetic)
    // Default to is_ascii = 0 (conservative - formatted output may contain UTF-8 from %S args)
    return (FlString) { .data = (char*)scratch->ptr + start_pos, .length = ctx.total_len, .is_ascii = 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString vsprintf_arena(FlArena* arena, const char* format, va_list args) {
    // The conflict argument keeps the assembly off arena itself when a caller formats into its own
    // scratch scope - the other scratch arena is used instead.
    arena_scratch_auto_conflict(scratch, arena);

    const FlString assembled = sprintf_assemble(scratch.arena, format, args);

    if (assembled.length == 0) {
        return (FlString) { .data = "", .length = 0, .is_ascii = 0 };
    }

    char* dest = arena_alloc_array(arena, char, assembled.length);
    memory_copy(dest, assembled.length, assembled.data, assembled.length);

    return (FlString) { .data = dest, .length = assembled.length, .is_ascii = 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_sprintf_arena(FlArena* arena, const char* format, ...) {
    va_list args;
    va_start(args, format);
    FlString result = vsprintf_arena(arena, format, args);
    va_end(args);
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Multi-Threaded Arena Sprintf Implementation

FlString vsprintf_arena_mt(FlArenaMt* arena_mt, const char* format, va_list args) {
    arena_scratch_auto(scratch);

    const FlString assembled = sprintf_assemble(scratch.arena, format, args);

    if (assembled.length == 0) {
        return (FlString) { .data = "", .length = 0, .is_ascii = 0 };
    }

    char* dest = arena_mt_alloc_array(arena_mt, char, assembled.length);
    memory_copy(dest, assembled.length, assembled.data, assembled.length);

    return (FlString) { .data = dest, .length = assembled.length, .is_ascii = 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString sprintf_arena_mt(FlArenaMt* arena_mt, const char* format, ...) {
    va_list args;
    va_start(args, format);
    FlString result = vsprintf_arena_mt(arena_mt, format, args);
    va_end(args);
    return result;
}

