#include "utest/utest.h"
#include <core/string.h>
#include <float.h>
#include <limits.h>

// Helper macro to avoid sign comparison warnings
#define ASSERT_STATUS_EQ(actual, expected) ASSERT_EQ((int)(actual), (int)(expected))

UTEST(string_parse, parse_u64_valid) {
    StringParseResultU64 result = fl_string_parse_u64(FL_S("12345"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 12345ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_u64_zero) {
    StringParseResultU64 result = fl_string_parse_u64(FL_S("0"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 0ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_u64_max) {
    StringParseResultU64 result = fl_string_parse_u64(FL_S("18446744073709551615"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, UINT64_MAX);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_u64_empty) {
    StringParseResultU64 result = fl_string_parse_u64(FL_S(""));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Empty);
    ASSERT_EQ(result.value, 0ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_u64_invalid_format) {
    StringParseResultU64 result = fl_string_parse_u64(FL_S("123abc"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_InvalidFormat);
    ASSERT_EQ(result.value, 0ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_u64_overflow) {
    StringParseResultU64 result = fl_string_parse_u64(FL_S("18446744073709551616")); // UINT64_MAX + 1
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Overflow);
    ASSERT_EQ(result.value, 0ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_i64_positive) {
    StringParseResultI64 result = fl_string_parse_i64(FL_S("12345"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 12345LL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_i64_negative) {
    StringParseResultI64 result = fl_string_parse_i64(FL_S("-12345"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, -12345LL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_i64_zero) {
    StringParseResultI64 result = fl_string_parse_i64(FL_S("0"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 0LL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_i64_max) {
    StringParseResultI64 result = fl_string_parse_i64(FL_S("9223372036854775807"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, INT64_MAX);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_i64_min) {
    StringParseResultI64 result = fl_string_parse_i64(FL_S("-9223372036854775808"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, INT64_MIN);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_u32_valid) {
    StringParseResultU32 result = fl_string_parse_u32(FL_S("4294967295"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, UINT32_MAX);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_u32_overflow) {
    StringParseResultU32 result = fl_string_parse_u32(FL_S("4294967296")); // UINT32_MAX + 1
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Overflow);
    ASSERT_EQ(result.value, 0U);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_i32_valid) {
    StringParseResultI32 result = fl_string_parse_i32(FL_S("2147483647"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, INT32_MAX);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_i32_overflow) {
    StringParseResultI32 result = fl_string_parse_i32(FL_S("2147483648")); // INT32_MAX + 1
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Overflow);
    ASSERT_EQ(result.value, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_i32_underflow) {
    StringParseResultI32 result = fl_string_parse_i32(FL_S("-2147483649")); // INT32_MIN - 1
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Underflow);
    ASSERT_EQ(result.value, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_f64_valid) {
    StringParseResultF64 result = fl_string_parse_f64(FL_S("123.456"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_NEAR(result.value, 123.456, 0.001);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_f64_negative) {
    StringParseResultF64 result = fl_string_parse_f64(FL_S("-123.456"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_NEAR(result.value, -123.456, 0.001);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_f64_zero) {
    StringParseResultF64 result = fl_string_parse_f64(FL_S("0.0"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_NEAR(result.value, 0.0, 0.001);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_f64_scientific) {
    StringParseResultF64 result = fl_string_parse_f64(FL_S("1.23e+10"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_NEAR(result.value, 1.23e+10, 1e6);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_f64_invalid) {
    StringParseResultF64 result = fl_string_parse_f64(FL_S("not_a_number"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_InvalidFormat);
    ASSERT_NEAR(result.value, 0.0, 0.001);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_f32_valid) {
    StringParseResultF32 result = fl_string_parse_f32(FL_S("123.456"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_NEAR(result.value, 123.456f, 0.001f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_f32_overflow) {
    StringParseResultF32 result = fl_string_parse_f32(FL_S("1e+50")); // Way larger than FLT_MAX
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Overflow);
    ASSERT_NEAR(result.value, 0.0f, 0.001f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_hex_u64_with_prefix) {
    StringParseResultU64 result = fl_string_parse_hex_u64(FL_S("0x1234ABCD"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 0x1234ABCDULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_hex_u64_without_prefix) {
    StringParseResultU64 result = fl_string_parse_hex_u64(FL_S("1234ABCD"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 0x1234ABCDULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_hex_u64_uppercase) {
    StringParseResultU64 result = fl_string_parse_hex_u64(FL_S("0X1234ABCD"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 0x1234ABCDULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_hex_u64_lowercase) {
    StringParseResultU64 result = fl_string_parse_hex_u64(FL_S("0x1234abcd"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 0x1234abcdULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_hex_u64_invalid_prefix) {
    StringParseResultU64 result = fl_string_parse_hex_u64(FL_S("0x"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_InvalidHexPrefix);
    ASSERT_EQ(result.value, 0ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_hex_u64_invalid_character) {
    StringParseResultU64 result = fl_string_parse_hex_u64(FL_S("0x123G"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_InvalidFormat);
    ASSERT_EQ(result.value, 0ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_hex_u64_max) {
    StringParseResultU64 result = fl_string_parse_hex_u64(FL_S("0xFFFFFFFFFFFFFFFF"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, UINT64_MAX);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_hex_u64_overflow) {
    StringParseResultU64 result = fl_string_parse_hex_u64(FL_S("0x10000000000000000")); // 17 hex digits
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Overflow);
    ASSERT_EQ(result.value, 0ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_hex_u32_valid) {
    StringParseResultU32 result = fl_string_parse_hex_u32(FL_S("0x80000000"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 0x80000000U);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_hex_u32_overflow) {
    StringParseResultU32 result = fl_string_parse_hex_u32(FL_S("0x100000000")); // Larger than u32
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Overflow);
    ASSERT_EQ(result.value, 0U);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_number_u64_decimal) {
    StringParseResultU64 result = fl_string_parse_number_u64(FL_S("12345"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 12345ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_number_u64_hex) {
    StringParseResultU64 result = fl_string_parse_number_u64(FL_S("0x1234ABCD"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 0x1234ABCDULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_number_u32_decimal) {
    StringParseResultU32 result = fl_string_parse_number_u32(FL_S("12345"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 12345U);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_number_u32_hex) {
    StringParseResultU32 result = fl_string_parse_number_u32(FL_S("0x80000000"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 0x80000000U);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_empty_string) {
    StringParseResultU64 result = fl_string_parse_u64(FL_S(""));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Empty);
    ASSERT_EQ(result.value, 0ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_null_data) {
    FlString null_str = { .data = NULL, .length = 5 };
    StringParseResultU64 result = fl_string_parse_u64(null_str);
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Empty);
    ASSERT_EQ(result.value, 0ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_leading_whitespace) {
    StringParseResultU64 result = fl_string_parse_u64(FL_S(" 123"));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_Success);
    ASSERT_EQ(result.value, 123ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_parse, parse_trailing_whitespace) {
    StringParseResultU64 result = fl_string_parse_u64(FL_S("123 "));
    ASSERT_STATUS_EQ(result.status, StringParseStatus_InvalidFormat);
    ASSERT_EQ(result.value, 0ULL);
}