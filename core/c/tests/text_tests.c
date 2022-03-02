#include "../src/font_private.h"
#include "../src/text.h"
#include "utest.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Text, test_utf8_decode_ascii) {
    u8 temp_buffer[256];
    LinearAllocator alloc;
    LinearAllocator_create(&alloc, "temp", temp_buffer, sizeof(temp_buffer));

    Utf8Result res = Utf8_to_codepoints_u32(&alloc, (u8*)"abcd", strlen("abcd"));
    ASSERT_EQ(res.error, FlError_None);
    ASSERT_NE(NULL, res.codepoints);
    ASSERT_EQ(4, res.len);

    ASSERT_EQ('a', res.codepoints[0]);
    ASSERT_EQ('b', res.codepoints[1]);
    ASSERT_EQ('c', res.codepoints[2]);
    ASSERT_EQ('d', res.codepoints[3]);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Text, test_utf8_decode_illegal) {
    u8 temp_buffer[8192];
    LinearAllocator alloc;
    LinearAllocator_create(&alloc, "temp", temp_buffer, sizeof(temp_buffer));

    // Taken from https://www.w3.org/2001/06/utf-8-wrong/UTF-8-test.html
    // TODO: Add the whole set

    ASSERT_EQ(FlError_Utf8Malformed, Utf8_to_codepoints_u32(&alloc, (u8*)"\xff", 1).error);
    ASSERT_EQ(FlError_Utf8Malformed, Utf8_to_codepoints_u32(&alloc, (u8*)"\xc0\xaf", 2).error);
    ASSERT_EQ(FlError_Utf8Malformed, Utf8_to_codepoints_u32(&alloc, (u8*)"\xe0\x80\xaf", 3).error);
    ASSERT_EQ(FlError_Utf8Malformed, Utf8_to_codepoints_u32(&alloc, (u8*)"\xe0\x80\x80\xaf", 4).error);
    ASSERT_EQ(FlError_Utf8Malformed, Utf8_to_codepoints_u32(&alloc, (u8*)"\xe0\x80\x80\x80\xaf", 5).error);

    ASSERT_EQ(FlError_Utf8Malformed, Utf8_to_codepoints_u32(&alloc, (u8*)"\xed\xa0\x80", 3).error);
    ASSERT_EQ(FlError_Utf8Malformed, Utf8_to_codepoints_u32(&alloc, (u8*)"\xed\xad\xbf", 3).error);
    ASSERT_EQ(FlError_Utf8Malformed, Utf8_to_codepoints_u32(&alloc, (u8*)"\xed\xae\x80", 3).error);
    ASSERT_EQ(FlError_Utf8Malformed, Utf8_to_codepoints_u32(&alloc, (u8*)"\xed\xaf\xbf", 3).error);
    ASSERT_EQ(FlError_Utf8Malformed, Utf8_to_codepoints_u32(&alloc, (u8*)"\xed\xb0\x80", 3).error);
    ASSERT_EQ(FlError_Utf8Malformed, Utf8_to_codepoints_u32(&alloc, (u8*)"\xed\xbe\x80", 3).error);
    ASSERT_EQ(FlError_Utf8Malformed, Utf8_to_codepoints_u32(&alloc, (u8*)"\xed\xbf\xbf", 3).error);
}
