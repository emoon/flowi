#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/env_expand.h>
#include <core/os/os.h>
#include <core/string.h>
#include "scoped_env.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(env_expand, no_variables) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("no variables here");
    FlString result = expand_env_vars(arena, input);

    ASSERT_TRUE(fl_string_equals(result, input));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(env_expand, empty_string) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("");
    FlString result = expand_env_vars(arena, input);

    ASSERT_TRUE(fl_string_equals(result, input));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(env_expand, braced_variable) {
    SCOPED_ENV(env_test_var, "TEST_VAR", "hello");

    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("${TEST_VAR} world");
    FlString result = expand_env_vars(arena, input);

    ASSERT_TRUE(fl_string_equals(result, FL_S("hello world")));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(env_expand, unbraced_variable) {
    SCOPED_ENV(env_user_name, "USER_NAME", "john");

    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("$USER_NAME-data");
    FlString result = expand_env_vars(arena, input);

    ASSERT_TRUE(fl_string_equals(result, FL_S("john-data")));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(env_expand, multiple_variables) {
    SCOPED_ENV(env_a, "A", "foo");
    SCOPED_ENV(env_b, "B", "bar");

    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("${A}/${B}");
    FlString result = expand_env_vars(arena, input);

    ASSERT_TRUE(fl_string_equals(result, FL_S("foo/bar")));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(env_expand, undefined_variable_braced) {
    SCOPED_ENV(env_undefined_var, "UNDEFINED_VAR", nullptr);

    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("${UNDEFINED_VAR}/path");
    FlString result = expand_env_vars(arena, input);

    ASSERT_TRUE(fl_string_equals(result, FL_S("${UNDEFINED_VAR}/path")));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(env_expand, undefined_variable_unbraced) {
    SCOPED_ENV(env_missing, "MISSING", nullptr);

    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("$MISSING/file");
    FlString result = expand_env_vars(arena, input);

    ASSERT_TRUE(fl_string_equals(result, FL_S("$MISSING/file")));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(env_expand, adjacent_variables) {
    SCOPED_ENV(env_x, "X", "1");
    SCOPED_ENV(env_y, "Y", "2");

    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("${X}${Y}");
    FlString result = expand_env_vars(arena, input);

    ASSERT_TRUE(fl_string_equals(result, FL_S("12")));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(env_expand, mixed_braced_unbraced) {
    SCOPED_ENV(env_home, "HOME", "/home/user");
    SCOPED_ENV(env_file, "FILE", "data.txt");

    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("${HOME}/$FILE");
    FlString result = expand_env_vars(arena, input);

    ASSERT_TRUE(fl_string_equals(result, FL_S("/home/user/data.txt")));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(env_expand, trailing_dollar) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("price is $");
    FlString result = expand_env_vars(arena, input);

    ASSERT_TRUE(fl_string_equals(result, FL_S("price is $")));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(env_expand, unclosed_brace) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("${UNCLOSED");
    FlString result = expand_env_vars(arena, input);

    ASSERT_TRUE(fl_string_equals(result, FL_S("${UNCLOSED")));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(env_expand, empty_variable_name) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString input1 = FL_S("${}");
    FlString result1 = expand_env_vars(arena, input1);
    ASSERT_TRUE(fl_string_equals(result1, FL_S("${}")));

    FlString input2 = FL_S("prefix-$-suffix");
    FlString result2 = expand_env_vars(arena, input2);
    ASSERT_TRUE(fl_string_equals(result2, FL_S("prefix-$-suffix")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(env_expand, real_world_build_dir) {
    SCOPED_ENV(env_build_dir, "BUILD_DIR", "/home/user/app/build");

    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("${BUILD_DIR}/cores/example_core.so");
    FlString result = expand_env_vars(arena, input);

    ASSERT_TRUE(fl_string_equals(result, FL_S("/home/user/app/build/cores/example_core.so")));
    fl_arena_destroy(arena);
}
