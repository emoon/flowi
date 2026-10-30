#include "perf_scope.h"
#include "log.h"
#include "os/os.h"
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static thread_local PerfScopeThreadState s_perf_state = { 0 };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PerfScopeThreadState* perf_scope_get_thread_state(void) {
    return &s_perf_state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 fl_perf_scope_begin(const char* name) {
    PerfScopeThreadState* state = &s_perf_state;

    if (state->entry_count >= PERF_SCOPE_MAX_ENTRIES || state->stack_depth >= PERF_SCOPE_MAX_DEPTH) {
        state->overflow = true;
        return UINT32_MAX;
    }

    u64 now = get_monotonic_time_ns_os();
    u16 entry_index = (u16)state->entry_count;
    u16 depth = (u16)state->stack_depth;
    u16 parent_index = (depth > 0) ? state->stack[depth - 1].entry_index : 0;

    // Reserve entry slot (will be completed in fl_perf_scope_end)
    PerfScopeEntry* entry = &state->entries[entry_index];
    entry->name = name;
    entry->start_ns = now;
    entry->elapsed_ns = 0;
    entry->depth = depth;
    entry->parent_index = parent_index;
    state->entry_count++;

    PerfScopeOpen* open = &state->stack[depth];
    open->name = name;
    open->start_ns = now;
    open->entry_index = entry_index;
    state->stack_depth++;

    return depth;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_perf_scope_end(u32 stack_index) {
    // Sentinel from overflow - no-op
    if (stack_index == UINT32_MAX) {
        return;
    }

    PerfScopeThreadState* state = &s_perf_state;

    if (state->stack_depth == 0 || stack_index != state->stack_depth - 1) {
        return;
    }

    u64 now = get_monotonic_time_ns_os();
    PerfScopeOpen* open = &state->stack[stack_index];

    PerfScopeEntry* entry = &state->entries[open->entry_index];
    entry->elapsed_ns = now - open->start_ns;

    state->stack_depth--;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_perf_scope_frame_begin(void) {
    PerfScopeThreadState* state = &s_perf_state;
    state->entry_count = 0;
    state->stack_depth = 0;
    state->overflow = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void perf_scope_print_tree(PerfScopeThreadState* state) {
    if (state->entry_count == 0) {
        return;
    }

    float root_ms = (float)state->entries[0].elapsed_ns / 1e6f;
    log_warning("=== Frame budget exceeded (%.2f ms) ===", root_ms);

    for (u32 i = 0; i < state->entry_count; i++) {
        PerfScopeEntry* entry = &state->entries[i];
        float ms = (float)entry->elapsed_ns / 1e6f;

        char indent[PERF_SCOPE_MAX_DEPTH * 2 + 1];
        u32 indent_len = entry->depth * 2;
        if (indent_len > sizeof(indent) - 1) {
            indent_len = sizeof(indent) - 1;
        }
        memset(indent, ' ', indent_len);
        indent[indent_len] = '\0';

        log_warning("%s%s: %.2f ms", indent, entry->name, ms);
    }

    if (state->overflow) {
        log_warning("  (profiler overflow - some scopes were not recorded)");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_perf_scope_frame_end(void) {
    PerfScopeThreadState* state = &s_perf_state;

    if (!state->is_main_thread) {
        return;
    }

    // Check if root entry exceeds budget
    if (state->entry_count > 0 && state->entries[0].elapsed_ns > PERF_SCOPE_FRAME_BUDGET_NS) {
        perf_scope_print_tree(state);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_perf_scope_init(void) {
    s_perf_state.is_main_thread = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_perf_scope_destroy(void) {
    memset(&s_perf_state, 0, sizeof(s_perf_state));
}
