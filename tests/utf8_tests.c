#include "utest/utest.h"
#include <core/utf8.h>
#include <string.h>

static FlString non_ascii_string(const char* bytes, size_t length) {
    return (FlString) { .data = bytes, .length = length, .is_ascii = 0 };
}

static void expect_decode(int* utest_result, const char* bytes, size_t length, u32 want) {
    FlString str = non_ascii_string(bytes, length);

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, want);
}

static void expect_decode_invalid(int* utest_result, const char* bytes, size_t length) {
    expect_decode(utest_result, bytes, length, UTF8_REPLACEMENT_CHAR);
}

static void expect_not_valid(int* utest_result, const char* bytes, size_t length) {
    FlString str = non_ascii_string(bytes, length);
    ASSERT_FALSE(utf8_is_valid(str));
}

static void expect_iter_codepoints(int* utest_result, Utf8Iter* it, const u32* want, u32 count) {
    for (u32 i = 0; i < count; i++) {
        ASSERT_EQ(utf8_iter_next(it), want[i]);
    }
}

static void expect_encoded(int* utest_result, u32 codepoint, const u8* want, u32 want_length) {
    char buf[4];

    u32 len = utf8_encode(buf, codepoint);
    ASSERT_EQ(len, want_length);
    for (u32 i = 0; i < want_length; i++) {
        ASSERT_EQ((u8)buf[i], want[i]);
    }
}

typedef struct SequenceLengthCase {
    u8 lead;
    u32 length;
} SequenceLengthCase;

UTEST(utf8, decode_ascii_single) {
    FlString str = FL_S("A");
    u32 cp;
    const char* next = utf8_decode_safe(str.data, str.data + str.length, &cp);

    ASSERT_EQ(cp, (u32)'A');
    ASSERT_EQ(next, str.data + 1);
}

UTEST(utf8, decode_ascii_all) {
    const char* ascii
        = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    FlString str = { .data = ascii, .length = strlen(ascii), .is_ascii = 1 };

    const char* ptr = str.data;
    const char* end = str.data + str.length;
    const char* expected_ptr = ascii;

    while (ptr < end) {
        u32 cp;
        ptr = utf8_decode_safe(ptr, end, &cp);
        ASSERT_EQ(cp, (u32)(u8)*expected_ptr);
        expected_ptr++;
    }
}

UTEST(utf8, decode_ascii_control) {
    char buf[32];
    for (int i = 0; i < 32; i++) {
        buf[i] = (char)i;
    }
    FlString str = { .data = buf, .length = 32, .is_ascii = 1 };

    Utf8Iter it = utf8_iter(str);
    for (int i = 0; i < 32; i++) {
        u32 cp = utf8_iter_next(&it);
        ASSERT_EQ(cp, (u32)i);
    }
}

UTEST(utf8, decode_2byte_latin_extended) {
    // "Café": the 'é' is U+00E9, encoded as C3 A9
    const char* cafe = "Caf\xC3\xA9";
    FlString str = { .data = cafe, .length = 5, .is_ascii = 0 };

    const u32 want[] = { 'C', 'a', 'f', 0x00E9 }; // é last
    Utf8Iter it = utf8_iter(str);
    expect_iter_codepoints(utest_result, &it, want, sizeof_array(want));
    ASSERT_TRUE(utf8_iter_done(&it));
}

UTEST(utf8, decode_2byte_german) {
    const char* german = "M\xC3\xBCller"; // Müller
    FlString str = { .data = german, .length = 7, .is_ascii = 0 };

    u64 count = utf8_codepoint_count(str);
    ASSERT_EQ(count, (u64)6); // M ü l l e r

    const u32 want[] = { 'M', 0x00FC, 'l', 'l', 'e', 'r' }; // ü second
    Utf8Iter it = utf8_iter(str);
    expect_iter_codepoints(utest_result, &it, want, sizeof_array(want));
}

UTEST(utf8, decode_2byte_swedish) {
    // Swedish characters: å=U+00E5, ä=U+00E4, ö=U+00F6
    const char* swedish = "\xC3\xA5\xC3\xA4\xC3\xB6"; // åäö
    FlString str = { .data = swedish, .length = 6, .is_ascii = 0 };

    u64 count = utf8_codepoint_count(str);
    ASSERT_EQ(count, (u64)3);

    const u32 want[] = { 0x00E5, 0x00E4, 0x00F6 }; // å ä ö
    Utf8Iter it = utf8_iter(str);
    expect_iter_codepoints(utest_result, &it, want, sizeof_array(want));
}

UTEST(utf8, decode_2byte_boundary) {
    expect_decode(utest_result, "\xC2\x80", 2, 0x0080); // U+0080
    expect_decode(utest_result, "\xDF\xBF", 2, 0x07FF); // U+07FF
}

UTEST(utf8, decode_3byte_cjk) {
    // CJK character: 中 = U+4E2D = E4 B8 AD
    expect_decode(utest_result, "\xE4\xB8\xAD", 3, 0x4E2D);
}

UTEST(utf8, decode_3byte_mixed) {
    // Mixed ASCII and CJK: "Hi中文"
    const char* mixed = "Hi\xE4\xB8\xAD\xE6\x96\x87";
    FlString str = non_ascii_string(mixed, 8);

    u64 count = utf8_codepoint_count(str);
    ASSERT_EQ(count, (u64)4); // H i 中 文

    const u32 want[] = { 'H', 'i', 0x4E2D, 0x6587 }; // 中 文 last
    Utf8Iter it = utf8_iter(str);
    expect_iter_codepoints(utest_result, &it, want, sizeof_array(want));
}

UTEST(utf8, decode_3byte_euro) {
    // Euro sign: € = U+20AC = E2 82 AC
    expect_decode(utest_result, "\xE2\x82\xAC", 3, 0x20AC);
}

UTEST(utf8, decode_3byte_boundary) {
    expect_decode(utest_result, "\xE0\xA0\x80", 3, 0x0800); // U+0800
    expect_decode(utest_result, "\xEF\xBF\xBF", 3, 0xFFFF); // U+FFFF
}

UTEST(utf8, decode_4byte_emoji) {
    // Grinning face emoji: 😀 = U+1F600 = F0 9F 98 80
    expect_decode(utest_result, "\xF0\x9F\x98\x80", 4, 0x1F600);
}

UTEST(utf8, decode_4byte_musical) {
    // Musical symbol G clef: 𝄞 = U+1D11E = F0 9D 84 9E
    expect_decode(utest_result, "\xF0\x9D\x84\x9E", 4, 0x1D11E);
}

UTEST(utf8, decode_4byte_boundary) {
    expect_decode(utest_result, "\xF0\x90\x80\x80", 4, 0x10000);  // U+10000
    expect_decode(utest_result, "\xF4\x8F\xBF\xBF", 4, 0x10FFFF); // U+10FFFF
}

UTEST(utf8, decode_invalid_continuation_only) {
    // Continuation byte without lead byte (0x80-0xBF)
    expect_decode_invalid(utest_result, "\x80", 1);
}

UTEST(utf8, decode_invalid_truncated_2byte) {
    // Lead byte for 2-byte, no continuation
    expect_decode_invalid(utest_result, "\xC2", 1);
}

UTEST(utf8, decode_invalid_truncated_3byte) {
    // Start of 中, missing last byte
    expect_decode_invalid(utest_result, "\xE4\xB8", 2);
}

UTEST(utf8, decode_invalid_truncated_4byte) {
    // Start of emoji, missing last byte
    expect_decode_invalid(utest_result, "\xF0\x9F\x98", 3);
}

UTEST(utf8, decode_invalid_bad_continuation) {
    // Second byte should be 80-BF
    const char bytes[2] = { '\xC2', '\x00' };
    expect_decode_invalid(utest_result, bytes, 2);
}

UTEST(utf8, decode_invalid_fe_ff) {
    // 0xFE and 0xFF are never valid UTF-8 bytes
    expect_decode_invalid(utest_result, "\xFE", 1);
    expect_decode_invalid(utest_result, "\xFF", 1);
}

UTEST(utf8, decode_overlong_2byte) {
    // Overlong encoding of NUL (U+0000): C0 80 instead of 00
    expect_decode_invalid(utest_result, "\xC0\x80", 2);
}

UTEST(utf8, decode_overlong_2byte_slash) {
    // Overlong encoding of '/' (U+002F): C0 AF instead of 2F
    // A decoder that accepts it lets a path traversal slip past a check for '/'.
    expect_decode_invalid(utest_result, "\xC0\xAF", 2);
}

UTEST(utf8, decode_overlong_3byte) {
    // Overlong encoding of U+007F: E0 81 BF instead of 7F
    expect_decode_invalid(utest_result, "\xE0\x81\xBF", 3);
}

UTEST(utf8, decode_overlong_4byte) {
    // Overlong encoding of U+0041 ('A'): F0 80 81 81 instead of 41
    expect_decode_invalid(utest_result, "\xF0\x80\x81\x81", 4);
}

UTEST(utf8, decode_surrogate_high) {
    // High surrogate U+D800 = ED A0 80 (invalid in UTF-8)
    expect_decode_invalid(utest_result, "\xED\xA0\x80", 3);
}

UTEST(utf8, decode_surrogate_low) {
    // Low surrogate U+DC00 = ED B0 80 (invalid in UTF-8)
    expect_decode_invalid(utest_result, "\xED\xB0\x80", 3);
}

UTEST(utf8, decode_surrogate_max_high) {
    // Maximum high surrogate U+DBFF = ED AF BF
    expect_decode_invalid(utest_result, "\xED\xAF\xBF", 3);
}

UTEST(utf8, decode_surrogate_max_low) {
    // Maximum low surrogate U+DFFF = ED BF BF
    expect_decode_invalid(utest_result, "\xED\xBF\xBF", 3);
}

UTEST(utf8, decode_out_of_range) {
    // U+110000 is beyond Unicode maximum (F4 90 80 80)
    expect_decode_invalid(utest_result, "\xF4\x90\x80\x80", 4);
}

UTEST(utf8, iter_empty) {
    FlString str = string_empty();

    Utf8Iter it = utf8_iter(str);
    ASSERT_TRUE(utf8_iter_done(&it));
    ASSERT_EQ(utf8_iter_next(&it), (u32)0);
}

UTEST(utf8, iter_mixed) {
    // "Hëllo 世界 🌍": a mix of ASCII, 2-byte, 3-byte and 4-byte sequences
    const char* mixed = "H\xC3\xABllo \xE4\xB8\x96\xE7\x95\x8C \xF0\x9F\x8C\x8D";
    FlString str = { .data = mixed, .length = 18, .is_ascii = 0 };

    u64 count = utf8_codepoint_count(str);
    ASSERT_EQ(count, (u64)10); // H ë l l o   世 界   🌍

    u32 expected[] = { 'H', 0x00EB, 'l', 'l', 'o', ' ', 0x4E16, 0x754C, ' ', 0x1F30D };

    Utf8Iter it = utf8_iter(str);
    for (int i = 0; i < 10; i++) {
        ASSERT_FALSE(utf8_iter_done(&it));
        u32 cp = utf8_iter_next(&it);
        ASSERT_EQ(cp, expected[i]);
    }
    ASSERT_TRUE(utf8_iter_done(&it));
}

UTEST(utf8, codepoint_count_ascii) {
    FlString str = FL_S("Hello World");
    u64 count = utf8_codepoint_count(str);
    ASSERT_EQ(count, (u64)11);
}

UTEST(utf8, codepoint_count_empty) {
    FlString str = string_empty();
    u64 count = utf8_codepoint_count(str);
    ASSERT_EQ(count, (u64)0);
}

UTEST(utf8, codepoint_count_multibyte) {
    // "日本語" = 3 characters, 9 bytes
    const char* japanese = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
    FlString str = { .data = japanese, .length = 9, .is_ascii = 0 };

    u64 count = utf8_codepoint_count(str);
    ASSERT_EQ(count, (u64)3);
}

UTEST(utf8, encode_ascii) {
    expect_encoded(utest_result, 'A', (const u8[]) { 'A' }, 1);
    expect_encoded(utest_result, 0x7F, (const u8[]) { 0x7F }, 1);
}

UTEST(utf8, encode_2byte) {
    // U+00E9 (é) = C3 A9
    expect_encoded(utest_result, 0x00E9, (const u8[]) { 0xC3, 0xA9 }, 2);

    // U+07FF (last 2-byte) = DF BF
    expect_encoded(utest_result, 0x07FF, (const u8[]) { 0xDF, 0xBF }, 2);
}

UTEST(utf8, encode_3byte) {
    // U+4E2D (中) = E4 B8 AD
    expect_encoded(utest_result, 0x4E2D, (const u8[]) { 0xE4, 0xB8, 0xAD }, 3);

    // U+20AC (€) = E2 82 AC
    expect_encoded(utest_result, 0x20AC, (const u8[]) { 0xE2, 0x82, 0xAC }, 3);
}

UTEST(utf8, encode_4byte) {
    // U+1F600 (😀) = F0 9F 98 80
    expect_encoded(utest_result, 0x1F600, (const u8[]) { 0xF0, 0x9F, 0x98, 0x80 }, 4);

    // U+10FFFF (max valid) = F4 8F BF BF
    expect_encoded(utest_result, 0x10FFFF, (const u8[]) { 0xF4, 0x8F, 0xBF, 0xBF }, 4);
}

UTEST(utf8, encode_invalid) {
    expect_encoded(utest_result, 0xD800, nullptr, 0);
    expect_encoded(utest_result, 0xDFFF, nullptr, 0);
    expect_encoded(utest_result, 0x110000, nullptr, 0);
}

UTEST(utf8, encode_decode_roundtrip) {
    u32 test_codepoints[] = {
        0x0000,  // NUL
        0x0041,  // 'A'
        0x007F,  // DEL (max ASCII)
        0x0080,  // First 2-byte
        0x00E9,  // é
        0x07FF,  // Last 2-byte
        0x0800,  // First 3-byte
        0x4E2D,  // 中
        0xFFFF,  // Last BMP
        0x10000, // First 4-byte
        0x1F600, // 😀
        0x10FFFF // Max Unicode
    };
    int num_tests = sizeof_array(test_codepoints);

    char buf[4];
    for (int i = 0; i < num_tests; i++) {
        u32 original = test_codepoints[i];

        u32 len = utf8_encode(buf, original);
        ASSERT_TRUE(len >= 1 && len <= 4);

        FlString str = { .data = buf, .length = len, .is_ascii = 0 };
        u32 decoded;
        utf8_decode_safe(str.data, str.data + str.length, &decoded);

        ASSERT_EQ(decoded, original);
    }
}

UTEST(utf8, is_valid_ascii) {
    FlString str = FL_S("Hello World 123");
    ASSERT_TRUE(utf8_is_valid(str));
}

UTEST(utf8, is_valid_utf8) {
    // "Café 日本語 😀"
    const char* valid = "Caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x98\x80";
    FlString str = { .data = valid, .length = strlen(valid), .is_ascii = 0 };
    ASSERT_TRUE(utf8_is_valid(str));
}

UTEST(utf8, is_valid_invalid) {
    // Bad continuation
    expect_not_valid(utest_result, (const char[2]) { '\xC2', '\x00' }, 2);

    // Overlong
    expect_not_valid(utest_result, (const char[2]) { '\xC0', '\x80' }, 2);

    // Surrogate
    expect_not_valid(utest_result, (const char[3]) { '\xED', '\xA0', '\x80' }, 3);
}

UTEST(utf8, is_valid_empty) {
    FlString str = string_empty();
    ASSERT_TRUE(utf8_is_valid(str));
}

UTEST(utf8, sequence_length) {
    const SequenceLengthCase cases[] = {
        { 0x00, 1 }, { 0x7F, 1 }, { 0xC2, 2 }, { 0xDF, 2 }, { 0xE0, 3 }, { 0xEF, 3 }, { 0xF0, 4 },
        { 0xF4, 4 },
        // Continuation, overlong leads, out of range
        { 0x80, 0 }, { 0xC0, 0 }, { 0xC1, 0 }, { 0xF5, 0 }, { 0xFE, 0 }, { 0xFF, 0 },
    };

    for (u32 i = 0; i < sizeof_array(cases); i++) {
        ASSERT_EQ(utf8_sequence_length(cases[i].lead), cases[i].length);
    }
}

UTEST(utf8, is_continuation) {
    const u8 continuations[] = { 0x80, 0x9F, 0xBF };
    const u8 non_continuations[] = { 0x00, 0x7F, 0xC0, 0xFF };

    for (u32 i = 0; i < sizeof_array(continuations); i++) {
        ASSERT_TRUE(utf8_is_continuation(continuations[i]));
    }
    for (u32 i = 0; i < sizeof_array(non_continuations); i++) {
        ASSERT_FALSE(utf8_is_continuation(non_continuations[i]));
    }
}
