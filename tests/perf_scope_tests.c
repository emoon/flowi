///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tests for the always-on lightweight frame profiler.

#include "utest/utest.h"
#include <core/core.h>
#include <core/os/os.h>
#include <core/perf_scope.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Burn enough time to be measurable on platforms with coarse timer resolution (macOS, arm64 Linux).

static void burn_measurable_time(void) {
    sleep_ms(1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(perf_scope, basic_scope) {
    fl_perf_scope_frame_begin();

    {
        perf_scope("test_scope");
        burn_measurable_time();
    }

    PerfScopeThreadState* state = perf_scope_get_thread_state();
    ASSERT_EQ(state->entry_count, (u32)1);
    ASSERT_STREQ(state->entries[0].name, "test_scope");
    ASSERT_EQ(state->entries[0].depth, (u16)0);
    ASSERT_GT(state->entries[0].elapsed_ns, (u64)0);
    ASSERT_FALSE(state->overflow);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(perf_scope, nested_scopes) {
    fl_perf_scope_frame_begin();

    {
        perf_scope("outer");
        {
            perf_scope("inner");
            volatile int x = 0;
            (void)x;
        }
    }

    PerfScopeThreadState* state = perf_scope_get_thread_state();
    ASSERT_EQ(state->entry_count, (u32)2);

    ASSERT_STREQ(state->entries[0].name, "outer");
    ASSERT_EQ(state->entries[0].depth, (u16)0);

    ASSERT_STREQ(state->entries[1].name, "inner");
    ASSERT_EQ(state->entries[1].depth, (u16)1);
    ASSERT_EQ(state->entries[1].parent_index, (u16)0);

    ASSERT_GE(state->entries[0].elapsed_ns, state->entries[1].elapsed_ns);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(perf_scope, frame_begin_resets) {
    fl_perf_scope_frame_begin();

    { perf_scope("first"); }

    PerfScopeThreadState* state = perf_scope_get_thread_state();
    ASSERT_EQ(state->entry_count, (u32)1);

    fl_perf_scope_frame_begin();
    ASSERT_EQ(state->entry_count, (u32)0);
    ASSERT_EQ(state->stack_depth, (u32)0);
    ASSERT_FALSE(state->overflow);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(perf_scope, entry_overflow) {
    fl_perf_scope_frame_begin();

    for (u32 i = 0; i < PERF_SCOPE_MAX_ENTRIES; i++) {
        u32 idx = fl_perf_scope_begin("fill");
        fl_perf_scope_end(idx);
    }

    PerfScopeThreadState* state = perf_scope_get_thread_state();
    ASSERT_EQ(state->entry_count, (u32)PERF_SCOPE_MAX_ENTRIES);

    u32 sentinel = fl_perf_scope_begin("overflow");
    ASSERT_EQ(sentinel, UINT32_MAX);
    ASSERT_TRUE(state->overflow);

    fl_perf_scope_end(sentinel);
    ASSERT_EQ(state->stack_depth, (u32)0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(perf_scope, stack_depth_overflow) {
    fl_perf_scope_frame_begin();

    u32 indices[PERF_SCOPE_MAX_DEPTH];
    for (u32 i = 0; i < PERF_SCOPE_MAX_DEPTH; i++) {
        indices[i] = fl_perf_scope_begin("deep");
        ASSERT_NE(indices[i], UINT32_MAX);
    }

    PerfScopeThreadState* state = perf_scope_get_thread_state();
    ASSERT_EQ(state->stack_depth, (u32)PERF_SCOPE_MAX_DEPTH);

    u32 overflow_idx = fl_perf_scope_begin("too_deep");
    ASSERT_EQ(overflow_idx, UINT32_MAX);
    ASSERT_TRUE(state->overflow);

    fl_perf_scope_end(overflow_idx);

    for (int i = PERF_SCOPE_MAX_DEPTH - 1; i >= 0; i--) {
        fl_perf_scope_end(indices[i]);
    }

    ASSERT_EQ(state->stack_depth, (u32)0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void helper_with_early_return(bool should_return) {
    perf_scope("helper");
    burn_measurable_time();
    if (should_return) {
        return; // Cleanup via __attribute__((cleanup))
    }
    volatile int x = 0;
    (void)x;
}

UTEST(perf_scope, cleanup_early_return) {
    fl_perf_scope_frame_begin();

    helper_with_early_return(true);
    helper_with_early_return(false);

    PerfScopeThreadState* state = perf_scope_get_thread_state();
    ASSERT_EQ(state->entry_count, (u32)2);
    ASSERT_EQ(state->stack_depth, (u32)0);

    ASSERT_STREQ(state->entries[0].name, "helper");
    ASSERT_STREQ(state->entries[1].name, "helper");
    ASSERT_GT(state->entries[0].elapsed_ns, (u64)0);
    ASSERT_GT(state->entries[1].elapsed_ns, (u64)0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(perf_scope, cleanup_goto) {
    fl_perf_scope_frame_begin();

    {
        perf_scope("goto_test");
        goto skip;
    skip:
        (void)0;
    }

    PerfScopeThreadState* state = perf_scope_get_thread_state();
    ASSERT_EQ(state->entry_count, (u32)1);
    ASSERT_EQ(state->stack_depth, (u32)0);
    ASSERT_STREQ(state->entries[0].name, "goto_test");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(perf_scope, sequential_siblings) {
    fl_perf_scope_frame_begin();

    {
        perf_scope("parent");
        { perf_scope("child_a"); }
        { perf_scope("child_b"); }
        { perf_scope("child_c"); }
    }

    PerfScopeThreadState* state = perf_scope_get_thread_state();
    ASSERT_EQ(state->entry_count, (u32)4);

    ASSERT_STREQ(state->entries[0].name, "parent");
    ASSERT_EQ(state->entries[0].depth, (u16)0);

    ASSERT_STREQ(state->entries[1].name, "child_a");
    ASSERT_EQ(state->entries[1].depth, (u16)1);
    ASSERT_EQ(state->entries[1].parent_index, (u16)0);

    ASSERT_STREQ(state->entries[2].name, "child_b");
    ASSERT_EQ(state->entries[2].depth, (u16)1);
    ASSERT_EQ(state->entries[2].parent_index, (u16)0);

    ASSERT_STREQ(state->entries[3].name, "child_c");
    ASSERT_EQ(state->entries[3].depth, (u16)1);
    ASSERT_EQ(state->entries[3].parent_index, (u16)0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void named_function_for_test(void) {
    perf_scope_function();
    volatile int x = 0;
    (void)x;
}

UTEST(perf_scope, scope_function_captures_func) {
    fl_perf_scope_frame_begin();

    named_function_for_test();

    PerfScopeThreadState* state = perf_scope_get_thread_state();
    ASSERT_EQ(state->entry_count, (u32)1);
    ASSERT_STREQ(state->entries[0].name, "named_function_for_test");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(perf_scope, deep_nesting_depths) {
    fl_perf_scope_frame_begin();

    u32 idx0 = fl_perf_scope_begin("d0");
    u32 idx1 = fl_perf_scope_begin("d1");
    u32 idx2 = fl_perf_scope_begin("d2");
    u32 idx3 = fl_perf_scope_begin("d3");

    fl_perf_scope_end(idx3);
    fl_perf_scope_end(idx2);
    fl_perf_scope_end(idx1);
    fl_perf_scope_end(idx0);

    PerfScopeThreadState* state = perf_scope_get_thread_state();
    ASSERT_EQ(state->entry_count, (u32)4);
    ASSERT_EQ(state->entries[0].depth, (u16)0);
    ASSERT_EQ(state->entries[1].depth, (u16)1);
    ASSERT_EQ(state->entries[2].depth, (u16)2);
    ASSERT_EQ(state->entries[3].depth, (u16)3);

    ASSERT_EQ(state->entries[1].parent_index, (u16)0);
    ASSERT_EQ(state->entries[2].parent_index, (u16)1);
    ASSERT_EQ(state->entries[3].parent_index, (u16)2);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(perf_scope, loop_scopes) {
    fl_perf_scope_frame_begin();

    for (int i = 0; i < 5; i++) {
        perf_scope("iteration");
    }

    PerfScopeThreadState* state = perf_scope_get_thread_state();
    ASSERT_EQ(state->entry_count, (u32)5);
    ASSERT_EQ(state->stack_depth, (u32)0);

    for (u32 i = 0; i < 5; i++) {
        ASSERT_STREQ(state->entries[i].name, "iteration");
        ASSERT_EQ(state->entries[i].depth, (u16)0);
    }
}
