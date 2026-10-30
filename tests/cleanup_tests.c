///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Automatic Cleanup Tests

#include "utest/utest.h"
#include <core/arena.h>
#include <core/assert.h>
#include <core/core.h>
#include <core/log.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cleanup, arena_scratch_auto_basic) {
    FlArena* scratch_arena = fl_arena_scratch_begin().arena;
    u64 start_pos = scratch_arena->pos;

    {
        arena_scratch_auto(temp);
        ASSERT_TRUE(temp.arena != NULL);

        char* buffer = arena_alloc_array(temp.arena, char, 1024);
        ASSERT_TRUE(buffer != NULL);

        ASSERT_GT(temp.arena->pos, start_pos);
    }

    FlArena* after_arena = fl_arena_scratch_begin().arena;
    ASSERT_EQ(after_arena->pos, start_pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Returns the scratch position reached inside the scope, sampled before the early return leaves it.
static u64 scratch_auto_early_return_helper(void) {
    arena_scratch_auto(temp);
    int* data = arena_alloc_array(temp.arena, int, 100);
    if (data != NULL) {
        return temp.arena->pos;
    }
    return 0;
}

UTEST(cleanup, arena_scratch_auto_early_return) {
    FlArena* scratch_arena = fl_arena_scratch_begin().arena;
    u64 start_pos = scratch_arena->pos;

    u64 inner_pos = scratch_auto_early_return_helper();
    ASSERT_GT(inner_pos, start_pos);

    ASSERT_EQ(scratch_arena->pos, start_pos);

    {
        arena_scratch_auto(temp);
        int* data = arena_alloc_array(temp.arena, int, 100);
        ASSERT_TRUE(data != NULL);
    }

    ASSERT_EQ(scratch_arena->pos, start_pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cleanup, arena_scratch_auto_nested) {
    FlArena* scratch_arena = fl_arena_scratch_begin().arena;
    u64 start_pos = scratch_arena->pos;

    {
        arena_scratch_auto(outer);
        char* outer_buffer = arena_alloc_array(outer.arena, char, 512);
        ASSERT_TRUE(outer_buffer != NULL);
        u64 outer_pos = outer.arena->pos;

        {
            arena_scratch_auto(inner);
            char* inner_buffer = arena_alloc_array(inner.arena, char, 256);
            ASSERT_TRUE(inner_buffer != NULL);

            ASSERT_GT(inner.arena->pos, outer_pos);
        }

        ASSERT_EQ(outer.arena->pos, outer_pos);
    }

    ASSERT_EQ(scratch_arena->pos, start_pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cleanup, arena_scratch_auto_multiple_goto_paths) {
    FlArena* scratch_arena = fl_arena_scratch_begin().arena;
    u64 start_pos = scratch_arena->pos;

    {
        arena_scratch_auto(temp);
        int* data = arena_alloc_array(temp.arena, int, 50);
        ASSERT_TRUE(data != NULL);
        goto path1_exit;
    }
path1_exit:
    ASSERT_EQ(scratch_arena->pos, start_pos);

    {
        arena_scratch_auto(temp);
        int* data = arena_alloc_array(temp.arena, int, 50);
        ASSERT_TRUE(data != NULL);
        goto path2_exit;
    }
path2_exit:
    ASSERT_EQ(scratch_arena->pos, start_pos);

    {
        arena_scratch_auto(temp);
        int* data = arena_alloc_array(temp.arena, int, 50);
        ASSERT_TRUE(data != NULL);
    }
    ASSERT_EQ(scratch_arena->pos, start_pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cleanup, arena_temp_auto_basic) {
    FlArena* test_arena = arena_new(.reserved_size = MB(64));
    u64 start_pos = test_arena->pos;

    {
        arena_temp_auto(temp, test_arena);
        ASSERT_EQ(temp.arena, test_arena);
        ASSERT_EQ(temp.pos, start_pos);

        int* numbers = arena_alloc_array(test_arena, int, 10);
        ASSERT_TRUE(numbers != NULL);
        for (int i = 0; i < 10; i++) {
            numbers[i] = i;
        }

        ASSERT_GT(test_arena->pos, start_pos);
    }

    ASSERT_EQ(test_arena->pos, start_pos);

    fl_arena_destroy(test_arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cleanup, arena_temp_auto_always_cleans) {
    FlArena* test_arena = arena_new(.reserved_size = MB(64));
    u64 start_pos = test_arena->pos;

    {
        arena_temp_auto(temp, test_arena);

        int* numbers = arena_alloc_array(test_arena, int, 10);
        ASSERT_TRUE(numbers != NULL);
    }

    ASSERT_EQ(test_arena->pos, start_pos);

    // If you want to KEEP allocations, don't use temp scope - just allocate directly
    {
        int* numbers = arena_alloc_array(test_arena, int, 10);
        ASSERT_TRUE(numbers != NULL);
    }

    ASSERT_GT(test_arena->pos, start_pos);

    fl_arena_destroy(test_arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cleanup, arena_scratch_auto_loop_control) {
    FlArena* scratch_arena = fl_arena_scratch_begin().arena;
    u64 start_pos = scratch_arena->pos;

    for (int i = 0; i < 10; i++) {
        arena_scratch_auto(temp);
        char* buffer = arena_alloc_array(temp.arena, char, 100);
        ASSERT_TRUE(buffer != NULL);

        if (i == 5) {
            break;
        }
    }

    ASSERT_EQ(scratch_arena->pos, start_pos);

    for (int i = 0; i < 10; i++) {
        arena_scratch_auto(temp);
        char* buffer = arena_alloc_array(temp.arena, char, 100);
        ASSERT_TRUE(buffer != NULL);

        if (i % 2 == 0) {
            continue;
        }
    }

    ASSERT_EQ(scratch_arena->pos, start_pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cleanup, arena_scratch_auto_goto) {
    FlArena* scratch_arena = fl_arena_scratch_begin().arena;
    u64 start_pos = scratch_arena->pos;

    bool error = false;

    {
        arena_scratch_auto(temp);
        char* buffer = arena_alloc_array(temp.arena, char, 200);
        ASSERT_TRUE(buffer != NULL);

        error = true;
        if (error) {
            goto cleanup;
        }

        // This shouldn't execute
        ASSERT_FALSE(true);
    }

cleanup:
    ASSERT_EQ(scratch_arena->pos, start_pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cleanup, arena_temp_auto_multiple_scopes) {
    FlArena* test_arena = arena_new(.reserved_size = MB(64));
    u64 start_pos = test_arena->pos;

    {
        arena_temp_auto(temp1, test_arena);
        int* data1 = arena_alloc_array(test_arena, int, 10);
        ASSERT_TRUE(data1 != NULL);
    }
    ASSERT_EQ(test_arena->pos, start_pos);

    {
        arena_temp_auto(temp2, test_arena);
        int* data2 = arena_alloc_array(test_arena, int, 20);
        ASSERT_TRUE(data2 != NULL);
    }
    ASSERT_EQ(test_arena->pos, start_pos);

    {
        arena_temp_auto(temp3, test_arena);
        int* data3 = arena_alloc_array(test_arena, int, 30);
        ASSERT_TRUE(data3 != NULL);
    }
    ASSERT_EQ(test_arena->pos, start_pos);

    fl_arena_destroy(test_arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cleanup, arena_scratch_manual_and_auto_separate) {
    FlArena* scratch_arena = fl_arena_scratch_begin().arena;
    u64 start_pos = scratch_arena->pos;

    {
        FlTempArena manual_temp = fl_arena_scratch_begin();
        char* buffer1 = arena_alloc_array(manual_temp.arena, char, 100);
        ASSERT_TRUE(buffer1 != NULL);
        fl_arena_scratch_end(manual_temp);
    }
    ASSERT_EQ(scratch_arena->pos, start_pos);

    {
        arena_scratch_auto(auto_temp);
        char* buffer2 = arena_alloc_array(auto_temp.arena, char, 100);
        ASSERT_TRUE(buffer2 != NULL);
    }
    ASSERT_EQ(scratch_arena->pos, start_pos);

    {
        FlTempArena manual_temp2 = fl_arena_scratch_begin();
        char* buffer3 = arena_alloc_array(manual_temp2.arena, char, 100);
        ASSERT_TRUE(buffer3 != NULL);
        fl_arena_scratch_end(manual_temp2);
    }
    ASSERT_EQ(scratch_arena->pos, start_pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cleanup, arena_scratch_auto_complex_control_flow) {
    FlArena* scratch_arena = fl_arena_scratch_begin().arena;
    u64 start_pos = scratch_arena->pos;

    {
        arena_scratch_auto(temp);
        char* buffer = arena_alloc_array(temp.arena, char, 500);
        ASSERT_TRUE(buffer != NULL);

        int mode = 1;
        switch (mode) {
            case 1:
                goto switch_exit;
            default:
                break;
        }
    }
switch_exit:
    ASSERT_EQ(scratch_arena->pos, start_pos);

    {
        arena_scratch_auto(temp);
        char* buffer = arena_alloc_array(temp.arena, char, 500);
        ASSERT_TRUE(buffer != NULL);

        for (int i = 0; i < 10; i++) {
            if (i == 5) {
                break;
            }
        }
    }
    ASSERT_EQ(scratch_arena->pos, start_pos);

    {
        arena_scratch_auto(temp);
        char* buffer = arena_alloc_array(temp.arena, char, 500);
        ASSERT_TRUE(buffer != NULL);

        if (true) {
            goto nested_exit;
        }
    }
nested_exit:
    ASSERT_EQ(scratch_arena->pos, start_pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cleanup, arena_scratch_auto_large_allocation) {
    FlArena* scratch_arena = fl_arena_scratch_begin().arena;
    u64 start_pos = scratch_arena->pos;

    {
        arena_scratch_auto(temp);

        char* large_buffer = arena_alloc_array(temp.arena, char, 1024 * 1024);
        ASSERT_TRUE(large_buffer != NULL);

        // Fill with data to ensure it's committed
        for (int i = 0; i < 1024; i++) {
            large_buffer[i] = (char)i;
        }
    }

    ASSERT_EQ(scratch_arena->pos, start_pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cleanup, arena_scratch_auto_zero_size) {
    FlArena* scratch_arena = fl_arena_scratch_begin().arena;
    u64 start_pos = scratch_arena->pos;

    {
        arena_scratch_auto(temp);

        (void)temp;
    }

    ASSERT_EQ(scratch_arena->pos, start_pos);
}
