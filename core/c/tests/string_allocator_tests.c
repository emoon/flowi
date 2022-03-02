#include <assert.h>
#include "../src/internal.h"
#include "../src/string_allocator.h"
#include "utest.h"

extern FlAllocator g_malloc_allocator;
static LinearAllocator s_frame_allocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static LinearAllocator* get_frame_allocator() {
    LinearAllocator_create_with_allocator(&s_frame_allocator, "frame_allocator", &g_malloc_allocator, 4096, true);
    return &s_frame_allocator;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void destroy_frame_allocator(StringAllocator* str_allocator) {
    LinearAllocator_destroy(str_allocator->frame_allocator);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(StringAllocator, create_destroy) {
    StringAllocator str_allocator;

    ASSERT_TRUE(StringAllocator_create(&str_allocator, &g_malloc_allocator, get_frame_allocator()));

    destroy_frame_allocator(&str_allocator);
    StringAllocator_destroy(&str_allocator);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(StringAllocator, copy_persistant) {
    StringAllocator str_alloc;

    const char* test_str = "test";

    ASSERT_TRUE(StringAllocator_create(&str_alloc, &g_malloc_allocator, get_frame_allocator()));
    FlString s = StringAllocator_copy_cstr(&str_alloc, test_str);

    ASSERT_NE(s.str, test_str);
    ASSERT_EQ(4, s.len);
    ASSERT_EQ(1, s.c_string);
    ASSERT_EQ(0, s.str[4]);
    ASSERT_EQ(1, str_alloc.string_count);

    destroy_frame_allocator(&str_alloc);
    StringAllocator_destroy(&str_alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(StringAllocator, copy_persistant_string) {
    StringAllocator str_alloc;

    FlString test_str = {"notfound", 1, strlen("notfound")};

    ASSERT_TRUE(StringAllocator_create(&str_alloc, &g_malloc_allocator, get_frame_allocator()));
    FlString s = StringAllocator_copy_string(&str_alloc, test_str);

    ASSERT_NE(s.str, test_str.str);
    ASSERT_EQ(test_str.len, s.len);
    ASSERT_EQ(1, s.c_string);
    ASSERT_EQ(0, s.str[test_str.len]);
    ASSERT_EQ(1, str_alloc.string_count);

    destroy_frame_allocator(&str_alloc);
    StringAllocator_destroy(&str_alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(StringAllocator, copy_persistant_manual_free) {
    StringAllocator str_alloc;

    const char* test_str = "test";

    ASSERT_TRUE(StringAllocator_create(&str_alloc, &g_malloc_allocator, get_frame_allocator()));
    FlString s = StringAllocator_copy_cstr(&str_alloc, test_str);

    ASSERT_NE(s.str, test_str);
    ASSERT_EQ(4, s.len);
    ASSERT_EQ(1, s.c_string);
    ASSERT_EQ(0, s.str[4]);
    ASSERT_EQ(1, str_alloc.string_count);

    StringAllocator_free_string(&str_alloc, s);
    ASSERT_EQ(0, str_alloc.string_count);

    destroy_frame_allocator(&str_alloc);
    StringAllocator_destroy(&str_alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(StringAllocator, copy_persistant_manual_free_2) {
    StringAllocator str_alloc;

    const char* test_str = "test";
    const char* test2_str = "foob";

    ASSERT_TRUE(StringAllocator_create(&str_alloc, &g_malloc_allocator, get_frame_allocator()));
    FlString s0 = StringAllocator_copy_cstr(&str_alloc, test_str);
    FlString s1 = StringAllocator_copy_cstr(&str_alloc, test2_str);
    ASSERT_EQ(2, str_alloc.string_count);

    StringAllocator_free_string(&str_alloc, s0);
    ASSERT_EQ(1, str_alloc.string_count);

    StringAllocator_free_string(&str_alloc, s1);
    ASSERT_EQ(0, str_alloc.string_count);

    destroy_frame_allocator(&str_alloc);
    StringAllocator_destroy(&str_alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(StringAllocator, copy_persistant_free_not_found) {
    StringAllocator str_alloc;

    const char* test_str = "test";

    ASSERT_TRUE(StringAllocator_create(&str_alloc, &g_malloc_allocator, get_frame_allocator()));
    StringAllocator_copy_cstr(&str_alloc, test_str);
    FlString dummy = {"notfound", 1, strlen("notfound")};
    ASSERT_EQ(1, str_alloc.string_count);

    StringAllocator_free_string(&str_alloc, dummy);
    ASSERT_EQ(1, str_alloc.string_count);  // shouldn't not have changed since dummy free

    destroy_frame_allocator(&str_alloc);
    StringAllocator_destroy(&str_alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(StringAllocator, copy_frame) {
    StringAllocator str_alloc;

    const char* test_str = "test";

    ASSERT_TRUE(StringAllocator_create(&str_alloc, &g_malloc_allocator, get_frame_allocator()));
    FlString s = StringAllocator_copy_cstr_frame(&str_alloc, test_str);

    // Expect that frame allocator and the string should point to the same memory for the first allocation
    ASSERT_EQ(s.str, (char*)str_alloc.frame_allocator->start_data);

    // Assume no persistant strings has been added
    ASSERT_EQ(0, str_alloc.string_count);

    destroy_frame_allocator(&str_alloc);
    StringAllocator_destroy(&str_alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(StringAllocator, copy_frame_string) {
    StringAllocator str_alloc;

    FlString dummy = {"notfound", 1, strlen("notfound")};

    ASSERT_TRUE(StringAllocator_create(&str_alloc, &g_malloc_allocator, get_frame_allocator()));
    FlString s = StringAllocator_copy_string_frame(&str_alloc, dummy);

    // Expect that frame allocator and the string should point to the same memory for the first allocation
    ASSERT_EQ(s.str, (char*)str_alloc.frame_allocator->start_data);

    // Assume no persistant strings has been added
    ASSERT_EQ(0, str_alloc.string_count);

    destroy_frame_allocator(&str_alloc);
    StringAllocator_destroy(&str_alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(StringAllocator, copy_frame_rewind) {
    StringAllocator str_alloc;

    const char* test_str = "test";
    const char* test2_str = "newstr";

    ASSERT_TRUE(StringAllocator_create(&str_alloc, &g_malloc_allocator, get_frame_allocator()));
    FlString s0 = StringAllocator_copy_cstr_frame(&str_alloc, test_str);

    // Expect these string to be the same
    ASSERT_STREQ(s0.str, (char*)str_alloc.frame_allocator->start_data);

    LinearAllocator_rewind(str_alloc.frame_allocator);

    // Expect allocator memory now to point at the new string
    FlString s1 = StringAllocator_copy_cstr_frame(&str_alloc, test2_str);
    ASSERT_STREQ(s1.str, (char*)str_alloc.frame_allocator->start_data);

    destroy_frame_allocator(&str_alloc);
    StringAllocator_destroy(&str_alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(StringAllocator, temp_copy_temp_buffer) {
    StringAllocator str_alloc;

    char temp_buffer[2048];
    FlString dummy = {"notfound", 0, strlen("notfound")};

    ASSERT_TRUE(StringAllocator_create(&str_alloc, &g_malloc_allocator, get_frame_allocator()));
    const char* str = StringAllocator_temp_string_to_cstr(&str_alloc, temp_buffer, sizeof(temp_buffer), dummy);

    // string should point to temp buffer
    ASSERT_EQ(str, (char*)&temp_buffer[0]);

    destroy_frame_allocator(&str_alloc);
    StringAllocator_destroy(&str_alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(StringAllocator, temp_copy_frame_allocator) {
    StringAllocator str_alloc;

    char temp_buffer[4];
    FlString dummy = {"notfound", 0, strlen("notfound")};

    ASSERT_TRUE(StringAllocator_create(&str_alloc, &g_malloc_allocator, get_frame_allocator()));
    const char* str = StringAllocator_temp_string_to_cstr(&str_alloc, temp_buffer, sizeof(temp_buffer), dummy);

    // Expect the string to end up in the frame allocator
    ASSERT_EQ(str, (char*)str_alloc.frame_allocator->start_data);

    destroy_frame_allocator(&str_alloc);
    StringAllocator_destroy(&str_alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(StringAllocator, temp_copy_is_cstr) {
    StringAllocator str_alloc;

    char temp_buffer[2048];
    FlString dummy = {"notfound", 1, strlen("notfound")};

    ASSERT_TRUE(StringAllocator_create(&str_alloc, &g_malloc_allocator, get_frame_allocator()));
    const char* str = StringAllocator_temp_string_to_cstr(&str_alloc, temp_buffer, sizeof(temp_buffer), dummy);

    // As it's a cstring we can just return
    ASSERT_EQ(str, dummy.str);

    destroy_frame_allocator(&str_alloc);
    StringAllocator_destroy(&str_alloc);
}