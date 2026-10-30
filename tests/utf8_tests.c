#include "utest/utest.h"
#include <core/utf8.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_ascii_single) {
    FlString str = FL_S("A");
    u32 cp;
    const char* next = utf8_decode_safe(str.data, str.data + str.length, &cp);

    ASSERT_EQ(cp, (u32)'A');
    ASSERT_EQ(next, str.data + 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_2byte_latin_extended) {
    // "Café" - the 'é' is U+00E9, encoded as C3 A9
    const char* cafe = "Caf\xC3\xA9";
    FlString str = { .data = cafe, .length = 5, .is_ascii = 0 };

    Utf8Iter it = utf8_iter(str);

    ASSERT_EQ(utf8_iter_next(&it), (u32)'C');
    ASSERT_EQ(utf8_iter_next(&it), (u32)'a');
    ASSERT_EQ(utf8_iter_next(&it), (u32)'f');
    ASSERT_EQ(utf8_iter_next(&it), (u32)0x00E9); // é
    ASSERT_TRUE(utf8_iter_done(&it));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_2byte_german) {
    const char* german = "M\xC3\xBCller"; // Müller
    FlString str = { .data = german, .length = 7, .is_ascii = 0 };

    u64 count = utf8_codepoint_count(str);
    ASSERT_EQ(count, (u64)6); // M ü l l e r

    Utf8Iter it = utf8_iter(str);
    ASSERT_EQ(utf8_iter_next(&it), (u32)'M');
    ASSERT_EQ(utf8_iter_next(&it), (u32)0x00FC); // ü
    ASSERT_EQ(utf8_iter_next(&it), (u32)'l');
    ASSERT_EQ(utf8_iter_next(&it), (u32)'l');
    ASSERT_EQ(utf8_iter_next(&it), (u32)'e');
    ASSERT_EQ(utf8_iter_next(&it), (u32)'r');
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_2byte_swedish) {
    // Swedish characters: å=U+00E5, ä=U+00E4, ö=U+00F6
    const char* swedish = "\xC3\xA5\xC3\xA4\xC3\xB6"; // åäö
    FlString str = { .data = swedish, .length = 6, .is_ascii = 0 };

    u64 count = utf8_codepoint_count(str);
    ASSERT_EQ(count, (u64)3);

    Utf8Iter it = utf8_iter(str);
    ASSERT_EQ(utf8_iter_next(&it), (u32)0x00E5); // å
    ASSERT_EQ(utf8_iter_next(&it), (u32)0x00E4); // ä
    ASSERT_EQ(utf8_iter_next(&it), (u32)0x00F6); // ö
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_2byte_boundary) {
    char buf[4];

    // U+0080
    buf[0] = '\xC2';
    buf[1] = '\x80';
    FlString str1 = { .data = buf, .length = 2, .is_ascii = 0 };
    u32 cp;
    utf8_decode_safe(str1.data, str1.data + str1.length, &cp);
    ASSERT_EQ(cp, (u32)0x0080);

    // U+07FF
    buf[0] = '\xDF';
    buf[1] = '\xBF';
    FlString str2 = { .data = buf, .length = 2, .is_ascii = 0 };
    utf8_decode_safe(str2.data, str2.data + str2.length, &cp);
    ASSERT_EQ(cp, (u32)0x07FF);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_3byte_cjk) {
    // CJK character: 中 = U+4E2D = E4 B8 AD
    const char* chinese = "\xE4\xB8\xAD";
    FlString str = { .data = chinese, .length = 3, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, (u32)0x4E2D);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_3byte_mixed) {
    // Mixed ASCII and CJK: "Hi中文"
    const char* mixed = "Hi\xE4\xB8\xAD\xE6\x96\x87";
    FlString str = { .data = mixed, .length = 8, .is_ascii = 0 };

    u64 count = utf8_codepoint_count(str);
    ASSERT_EQ(count, (u64)4); // H i 中 文

    Utf8Iter it = utf8_iter(str);
    ASSERT_EQ(utf8_iter_next(&it), (u32)'H');
    ASSERT_EQ(utf8_iter_next(&it), (u32)'i');
    ASSERT_EQ(utf8_iter_next(&it), (u32)0x4E2D); // 中
    ASSERT_EQ(utf8_iter_next(&it), (u32)0x6587); // 文
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_3byte_euro) {
    // Euro sign: € = U+20AC = E2 82 AC
    const char* euro = "\xE2\x82\xAC";
    FlString str = { .data = euro, .length = 3, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, (u32)0x20AC);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_3byte_boundary) {
    char buf[4];

    // U+0800
    buf[0] = '\xE0';
    buf[1] = '\xA0';
    buf[2] = '\x80';
    FlString str1 = { .data = buf, .length = 3, .is_ascii = 0 };
    u32 cp;
    utf8_decode_safe(str1.data, str1.data + str1.length, &cp);
    ASSERT_EQ(cp, (u32)0x0800);

    // U+FFFF
    buf[0] = '\xEF';
    buf[1] = '\xBF';
    buf[2] = '\xBF';
    FlString str2 = { .data = buf, .length = 3, .is_ascii = 0 };
    utf8_decode_safe(str2.data, str2.data + str2.length, &cp);
    ASSERT_EQ(cp, (u32)0xFFFF);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_4byte_emoji) {
    // Grinning face emoji: 😀 = U+1F600 = F0 9F 98 80
    const char* emoji = "\xF0\x9F\x98\x80";
    FlString str = { .data = emoji, .length = 4, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, (u32)0x1F600);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_4byte_musical) {
    // Musical symbol G clef: 𝄞 = U+1D11E = F0 9D 84 9E
    const char* clef = "\xF0\x9D\x84\x9E";
    FlString str = { .data = clef, .length = 4, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, (u32)0x1D11E);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_4byte_boundary) {
    char buf[4];

    // U+10000
    buf[0] = '\xF0';
    buf[1] = '\x90';
    buf[2] = '\x80';
    buf[3] = '\x80';
    FlString str1 = { .data = buf, .length = 4, .is_ascii = 0 };
    u32 cp;
    utf8_decode_safe(str1.data, str1.data + str1.length, &cp);
    ASSERT_EQ(cp, (u32)0x10000);

    // U+10FFFF
    buf[0] = '\xF4';
    buf[1] = '\x8F';
    buf[2] = '\xBF';
    buf[3] = '\xBF';
    FlString str2 = { .data = buf, .length = 4, .is_ascii = 0 };
    utf8_decode_safe(str2.data, str2.data + str2.length, &cp);
    ASSERT_EQ(cp, (u32)0x10FFFF);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_invalid_continuation_only) {
    // Continuation byte without lead byte (0x80-0xBF)
    char buf[1] = { '\x80' };
    FlString str = { .data = buf, .length = 1, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_invalid_truncated_2byte) {
    char buf[1] = { '\xC2' }; // Lead byte for 2-byte, no continuation
    FlString str = { .data = buf, .length = 1, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_invalid_truncated_3byte) {
    char buf[2] = { '\xE4', '\xB8' }; // Start of 中, missing last byte
    FlString str = { .data = buf, .length = 2, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_invalid_truncated_4byte) {
    char buf[3] = { '\xF0', '\x9F', '\x98' }; // Start of emoji, missing last byte
    FlString str = { .data = buf, .length = 3, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_invalid_bad_continuation) {
    char buf[2] = { '\xC2', '\x00' }; // Second byte should be 80-BF
    FlString str = { .data = buf, .length = 2, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_invalid_fe_ff) {
    // 0xFE and 0xFF are never valid UTF-8 bytes
    char buf[1];

    buf[0] = '\xFE';
    FlString str1 = { .data = buf, .length = 1, .is_ascii = 0 };
    u32 cp;
    utf8_decode_safe(str1.data, str1.data + str1.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);

    buf[0] = '\xFF';
    FlString str2 = { .data = buf, .length = 1, .is_ascii = 0 };
    utf8_decode_safe(str2.data, str2.data + str2.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_overlong_2byte) {
    // Overlong encoding of NUL (U+0000): C0 80 instead of 00
    char buf[2] = { '\xC0', '\x80' };
    FlString str = { .data = buf, .length = 2, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_overlong_2byte_slash) {
    // Overlong encoding of '/' (U+002F): C0 AF instead of 2F
    // This is a security issue if not caught (path traversal attacks)
    char buf[2] = { '\xC0', '\xAF' };
    FlString str = { .data = buf, .length = 2, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_overlong_3byte) {
    // Overlong encoding of U+007F: E0 81 BF instead of 7F
    char buf[3] = { '\xE0', '\x81', '\xBF' };
    FlString str = { .data = buf, .length = 3, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_overlong_4byte) {
    // Overlong encoding of U+0041 ('A'): F0 80 81 81 instead of 41
    char buf[4] = { '\xF0', '\x80', '\x81', '\x81' };
    FlString str = { .data = buf, .length = 4, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_surrogate_high) {
    // High surrogate U+D800 = ED A0 80 (invalid in UTF-8)
    char buf[3] = { '\xED', '\xA0', '\x80' };
    FlString str = { .data = buf, .length = 3, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_surrogate_low) {
    // Low surrogate U+DC00 = ED B0 80 (invalid in UTF-8)
    char buf[3] = { '\xED', '\xB0', '\x80' };
    FlString str = { .data = buf, .length = 3, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_surrogate_max_high) {
    // Maximum high surrogate U+DBFF = ED AF BF
    char buf[3] = { '\xED', '\xAF', '\xBF' };
    FlString str = { .data = buf, .length = 3, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_surrogate_max_low) {
    // Maximum low surrogate U+DFFF = ED BF BF
    char buf[3] = { '\xED', '\xBF', '\xBF' };
    FlString str = { .data = buf, .length = 3, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, decode_out_of_range) {
    // U+110000 is beyond Unicode maximum (F4 90 80 80)
    char buf[4] = { '\xF4', '\x90', '\x80', '\x80' };
    FlString str = { .data = buf, .length = 4, .is_ascii = 0 };

    u32 cp;
    utf8_decode_safe(str.data, str.data + str.length, &cp);
    ASSERT_EQ(cp, UTF8_REPLACEMENT_CHAR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, iter_empty) {
    FlString str = string_empty();

    Utf8Iter it = utf8_iter(str);
    ASSERT_TRUE(utf8_iter_done(&it));
    ASSERT_EQ(utf8_iter_next(&it), (u32)0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, iter_mixed) {
    // "Hëllo 世界 🌍" - mix of ASCII, 2-byte, 3-byte, and 4-byte
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, codepoint_count_ascii) {
    FlString str = FL_S("Hello World");
    u64 count = utf8_codepoint_count(str);
    ASSERT_EQ(count, (u64)11);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, codepoint_count_empty) {
    FlString str = string_empty();
    u64 count = utf8_codepoint_count(str);
    ASSERT_EQ(count, (u64)0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, codepoint_count_multibyte) {
    // "日本語" = 3 characters, 9 bytes
    const char* japanese = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
    FlString str = { .data = japanese, .length = 9, .is_ascii = 0 };

    u64 count = utf8_codepoint_count(str);
    ASSERT_EQ(count, (u64)3);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, encode_ascii) {
    char buf[4];

    u32 len = utf8_encode(buf, 'A');
    ASSERT_EQ(len, (u32)1);
    ASSERT_EQ((u8)buf[0], (u8)'A');

    len = utf8_encode(buf, 0x7F);
    ASSERT_EQ(len, (u32)1);
    ASSERT_EQ((u8)buf[0], (u8)0x7F);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, encode_2byte) {
    char buf[4];

    // U+00E9 (é) = C3 A9
    u32 len = utf8_encode(buf, 0x00E9);
    ASSERT_EQ(len, (u32)2);
    ASSERT_EQ((u8)buf[0], (u8)0xC3);
    ASSERT_EQ((u8)buf[1], (u8)0xA9);

    // U+07FF (last 2-byte) = DF BF
    len = utf8_encode(buf, 0x07FF);
    ASSERT_EQ(len, (u32)2);
    ASSERT_EQ((u8)buf[0], (u8)0xDF);
    ASSERT_EQ((u8)buf[1], (u8)0xBF);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, encode_3byte) {
    char buf[4];

    // U+4E2D (中) = E4 B8 AD
    u32 len = utf8_encode(buf, 0x4E2D);
    ASSERT_EQ(len, (u32)3);
    ASSERT_EQ((u8)buf[0], (u8)0xE4);
    ASSERT_EQ((u8)buf[1], (u8)0xB8);
    ASSERT_EQ((u8)buf[2], (u8)0xAD);

    // U+20AC (€) = E2 82 AC
    len = utf8_encode(buf, 0x20AC);
    ASSERT_EQ(len, (u32)3);
    ASSERT_EQ((u8)buf[0], (u8)0xE2);
    ASSERT_EQ((u8)buf[1], (u8)0x82);
    ASSERT_EQ((u8)buf[2], (u8)0xAC);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, encode_4byte) {
    char buf[4];

    // U+1F600 (😀) = F0 9F 98 80
    u32 len = utf8_encode(buf, 0x1F600);
    ASSERT_EQ(len, (u32)4);
    ASSERT_EQ((u8)buf[0], (u8)0xF0);
    ASSERT_EQ((u8)buf[1], (u8)0x9F);
    ASSERT_EQ((u8)buf[2], (u8)0x98);
    ASSERT_EQ((u8)buf[3], (u8)0x80);

    // U+10FFFF (max valid) = F4 8F BF BF
    len = utf8_encode(buf, 0x10FFFF);
    ASSERT_EQ(len, (u32)4);
    ASSERT_EQ((u8)buf[0], (u8)0xF4);
    ASSERT_EQ((u8)buf[1], (u8)0x8F);
    ASSERT_EQ((u8)buf[2], (u8)0xBF);
    ASSERT_EQ((u8)buf[3], (u8)0xBF);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, encode_invalid) {
    char buf[4];

    u32 len = utf8_encode(buf, 0xD800);
    ASSERT_EQ(len, (u32)0);

    len = utf8_encode(buf, 0xDFFF);
    ASSERT_EQ(len, (u32)0);

    len = utf8_encode(buf, 0x110000);
    ASSERT_EQ(len, (u32)0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
    int num_tests = sizeof(test_codepoints) / sizeof(test_codepoints[0]);

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, is_valid_ascii) {
    FlString str = FL_S("Hello World 123");
    ASSERT_TRUE(utf8_is_valid(str));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, is_valid_utf8) {
    // "Café 日本語 😀"
    const char* valid = "Caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x98\x80";
    FlString str = { .data = valid, .length = strlen(valid), .is_ascii = 0 };
    ASSERT_TRUE(utf8_is_valid(str));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, is_valid_invalid) {
    char buf1[2] = { '\xC2', '\x00' }; // Bad continuation
    FlString str1 = { .data = buf1, .length = 2, .is_ascii = 0 };
    ASSERT_FALSE(utf8_is_valid(str1));

    // Overlong
    char buf2[2] = { '\xC0', '\x80' };
    FlString str2 = { .data = buf2, .length = 2, .is_ascii = 0 };
    ASSERT_FALSE(utf8_is_valid(str2));

    // Surrogate
    char buf3[3] = { '\xED', '\xA0', '\x80' };
    FlString str3 = { .data = buf3, .length = 3, .is_ascii = 0 };
    ASSERT_FALSE(utf8_is_valid(str3));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, is_valid_empty) {
    FlString str = string_empty();
    ASSERT_TRUE(utf8_is_valid(str));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, sequence_length) {
    ASSERT_EQ(utf8_sequence_length(0x00), (u32)1);
    ASSERT_EQ(utf8_sequence_length(0x7F), (u32)1);

    ASSERT_EQ(utf8_sequence_length(0xC2), (u32)2);
    ASSERT_EQ(utf8_sequence_length(0xDF), (u32)2);

    ASSERT_EQ(utf8_sequence_length(0xE0), (u32)3);
    ASSERT_EQ(utf8_sequence_length(0xEF), (u32)3);

    ASSERT_EQ(utf8_sequence_length(0xF0), (u32)4);
    ASSERT_EQ(utf8_sequence_length(0xF4), (u32)4);

    ASSERT_EQ(utf8_sequence_length(0x80), (u32)0); // Continuation
    ASSERT_EQ(utf8_sequence_length(0xC0), (u32)0); // Overlong lead
    ASSERT_EQ(utf8_sequence_length(0xC1), (u32)0); // Overlong lead
    ASSERT_EQ(utf8_sequence_length(0xF5), (u32)0); // Out of range
    ASSERT_EQ(utf8_sequence_length(0xFE), (u32)0);
    ASSERT_EQ(utf8_sequence_length(0xFF), (u32)0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(utf8, is_continuation) {
    ASSERT_TRUE(utf8_is_continuation(0x80));
    ASSERT_TRUE(utf8_is_continuation(0x9F));
    ASSERT_TRUE(utf8_is_continuation(0xBF));

    ASSERT_FALSE(utf8_is_continuation(0x00));
    ASSERT_FALSE(utf8_is_continuation(0x7F));
    ASSERT_FALSE(utf8_is_continuation(0xC0));
    ASSERT_FALSE(utf8_is_continuation(0xFF));
}

// Test definitions only - main runner in base_tests.c
