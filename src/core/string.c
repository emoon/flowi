#include "string.h"

#include "assert.h"
#include "log.h"
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "memory.h"
#include "sprintf.h"

const char* fl_string_to_cstr(struct FlArena* arena, const FlString str) {
    char* temp = arena_alloc_array(arena, char, str.length + 1);
    memory_copy(temp, str.length + 1, str.data, str.length);
    temp[str.length] = '\0';
    return temp;
}

void fl_string_to_cstr_buffer(char* dest, const u64 buffer_len, const FlString str) {
    if (buffer_len == 0) {
        return; // No room for anything, not even the null terminator
    }
    const u64 copy_len = str.length < buffer_len - 1 ? str.length : buffer_len - 1;
    memory_copy(dest, buffer_len, str.data, copy_len);
    dest[copy_len] = '\0';
}

bool fl_string_begins_with(const FlString str, const FlString needle) {
    if (needle.length > str.length) {
        return false;
    }
    return memory_compare(str.data, needle.data, needle.length) == 0;
}

bool fl_string_ends_with(const FlString str, const FlString needle) {
    if (needle.length > str.length) {
        return false;
    }

    const char* str_end = str.data + str.length - needle.length;
    return memory_compare(str_end, needle.data, needle.length) == 0;
}

static inline char char_to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static inline bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool bytes_equal_nocase(const char* a, const char* b, u64 length) {
    for (u64 i = 0; i < length; i++) {
        if (char_to_lower(a[i]) != char_to_lower(b[i])) {
            return false;
        }
    }
    return true;
}

// -1, 0 or 1 as a is less than, equal to or greater than b
static int compare_u64(u64 a, u64 b) {
    return (a > b) - (a < b);
}

bool fl_string_equals(const FlString a, const FlString b) {
    if (a.length != b.length) {
        return false;
    }

    // Empty strings may have null data, and a shared pointer needs no byte compare
    if (a.length == 0 || a.data == b.data) {
        return true;
    }

    return memory_compare(a.data, b.data, a.length) == 0;
}

bool fl_string_equals_nocase(const FlString a, const FlString b) {
    if (a.length != b.length) {
        return false;
    }

    return bytes_equal_nocase(a.data, b.data, a.length);
}

int fl_string_compare_n(const FlString a, const FlString b, u64 max_len) {
    const u64 common_len = a.length < b.length ? a.length : b.length;

    // The length tie-break below is only meaningful when the full common prefix was compared; when max_len
    // cuts the comparison short, equal bytes within the bound mean "equal within the bound".
    if (max_len < common_len) {
        return memory_compare(a.data, b.data, max_len);
    }

    if (common_len > 0) {
        const int result = memory_compare(a.data, b.data, common_len);
        if (result != 0) {
            return result;
        }
    }

    return compare_u64(a.length, b.length);
}

// Consume the run of digits at *pos and return it without its leading zeros
static FlString take_number(const FlString str, u64* pos) {
    u64 i = *pos;
    while (i < str.length && str.data[i] == '0') {
        i++;
    }

    const u64 start = i;
    while (i < str.length && is_digit(str.data[i])) {
        i++;
    }

    *pos = i;
    return (FlString) { .data = str.data + start, .length = i - start };
}

// Compare two digit runs that have no leading zeros: the longer one is the bigger number
static int compare_numbers(const FlString a, const FlString b) {
    if (a.length != b.length) {
        return compare_u64(a.length, b.length);
    }

    for (u64 i = 0; i < a.length; i++) {
        if (a.data[i] != b.data[i]) {
            return a.data[i] < b.data[i] ? -1 : 1;
        }
    }
    return 0;
}

static int compare_chars_nocase(char a, char b) {
    const char lower_a = char_to_lower(a);
    const char lower_b = char_to_lower(b);
    return (lower_a > lower_b) - (lower_a < lower_b);
}

// Natural string comparison (case-insensitive, with numeric awareness)
// Compares strings in a human-friendly way: "file1" < "file2" < "file10"
int fl_string_compare_natural(const FlString a, const FlString b) {
    u64 i = 0;
    u64 j = 0;

    while (i < a.length && j < b.length) {
        int result;
        if (is_digit(a.data[i]) && is_digit(b.data[j])) {
            const FlString number_a = take_number(a, &i);
            const FlString number_b = take_number(b, &j);
            result = compare_numbers(number_a, number_b);
        } else {
            result = compare_chars_nocase(a.data[i++], b.data[j++]);
        }

        if (result != 0) {
            return result;
        }
    }

    // One string is exhausted; the one with characters left sorts last
    return (int)(i < a.length) - (int)(j < b.length);
}

FlString fl_string_concat(struct FlArena* arena, FlString s0, FlString s1) {
    const u64 total_length = s0.length + s1.length;
    char* buffer = arena_alloc_array(arena, char, total_length);

    memory_copy(buffer, total_length, s0.data, s0.length);
    memory_copy(buffer + s0.length, total_length - s0.length, s1.data, s1.length);

    return (FlString) { .data = buffer, .length = total_length, .is_ascii = s0.is_ascii && s1.is_ascii };
}

static inline FlString string_copy_impl(char* buffer, const FlString str) {
    memory_copy(buffer, str.length, str.data, str.length);
    return (FlString) { .data = buffer, .length = str.length, .is_ascii = str.is_ascii };
}

// Static strings are shared instead of copied, and an empty string owns no bytes
static bool copy_needs_allocation(const FlString str) {
    return !str.is_static && str.length > 0;
}

FlString fl_string_copy(struct FlArena* arena, const FlString str) {
    if (!copy_needs_allocation(str)) {
        return str.is_static ? str : string_empty();
    }

    return string_copy_impl(arena_alloc_array(arena, char, str.length), str);
}

FlString fl_string_copy_malloc(const FlString str) {
    if (!copy_needs_allocation(str)) {
        return str.is_static ? str : string_empty();
    }

    return string_copy_impl(mi_malloc(str.length), str);
}

FlString fl_string_to_lower(struct FlArena* arena, const FlString str) {
    if (str.length == 0) {
        return string_empty();
    }

    char* buffer = arena_alloc_array(arena, char, str.length);
    for (u64 i = 0; i < str.length; i++) {
        buffer[i] = char_to_lower(str.data[i]);
    }

    return (FlString) { .data = buffer, .length = str.length, .is_ascii = str.is_ascii };
}

// Static strings, empty strings (which point to "") and nullptr were never heap allocated
static bool owns_heap_data(const FlString str) {
    return str.length > 0 && !str.is_static && str.data;
}

void fl_string_free(FlString str) {
    if (owns_heap_data(str)) {
        mi_free((void*)str.data);
    }
}

/// FlStringBuilder implementation

FlStringBuilder fl_sb_create(struct FlArena* arena) {
    return (FlStringBuilder) { .arena = arena, .start_pos = arena->pos };
}

FlStringBuilder fl_sb_append_cstr(FlStringBuilder sb, const char* str) {
    if (!str)
        return sb;

    const u64 str_len = strlen(str);
    char* dest = arena_alloc_array(sb.arena, char, str_len);
    memory_copy(dest, str_len, str, str_len);

    return sb;
}

FlStringBuilder fl_sb_append_string(FlStringBuilder sb, FlString str) {
    if (str.length == 0)
        return sb;

    char* dest = arena_alloc_array(sb.arena, char, str.length);
    memory_copy(dest, str.length, str.data, str.length);

    return sb;
}

FlStringBuilder fl_sb_append_char(FlStringBuilder sb, char c) {
    char* dest = fl_arena_alloc_raw(sb.arena, 1, 1);
    *dest = c;

    return sb;
}

FlStringBuilder fl_sb_appendf(FlStringBuilder sb, const char* format, ...) {
    va_list args;
    va_start(args, format);
    FlStringBuilder result = sb_vappendf(sb, format, args);
    va_end(args);
    return result;
}

FlStringBuilder sb_vappendf(FlStringBuilder sb, const char* format, va_list args) {
    vsprintf_arena(sb.arena, format, args);
    return sb;
}

FlStringBuilder fl_sb_clear(FlStringBuilder sb) {
    sb.arena->pos = sb.start_pos;
    return sb;
}

FlString fl_sb_to_string(FlStringBuilder sb) {
    const u64 length = sb.arena->pos - sb.start_pos;
    const char* data = (const char*)sb.arena->ptr + sb.start_pos;
    // Conservative: FlStringBuilder does not track ASCII status.
    return (FlString) { .data = data, .length = length, .is_ascii = 0 };
}

const char* fl_sb_to_cstr(FlStringBuilder sb) {
    fl_arena_alloc_raw(sb.arena, 1, 1);
    char* null_term = (char*)sb.arena->ptr + sb.arena->pos - 1;
    *null_term = '\0';

    const char* data = (const char*)sb.arena->ptr + sb.start_pos;
    return data;
}

u64 fl_sb_length(FlStringBuilder sb) {
    return sb.arena->pos - sb.start_pos;
}

FlString fl_string_substr(const FlString str, u64 start, u64 length) {
    if (start >= str.length) {
        return string_empty();
    }

    // Compared against the bytes left rather than as start + length, which can wrap
    const u64 remaining = str.length - start;
    const u64 actual_length = length < remaining ? length : remaining;

    // Substring of ASCII string is ASCII
    return (FlString) { .data = str.data + start, .length = actual_length, .is_ascii = str.is_ascii };
}

// Number parsing

// Copy str into buffer as a C string for the strto* family and clear errno for the call. Anything but Success means
// there is nothing to parse.
static StringParseStatus parse_begin(const FlString str, char* buffer, u64 buffer_size) {
    if (string_is_empty(str)) {
        return StringParseStatus_Empty;
    }

    if (str.length >= buffer_size) {
        return StringParseStatus_InvalidFormat; // Too long to be a number
    }

    memory_copy(buffer, buffer_size, str.data, str.length);
    buffer[str.length] = '\0';
    errno = 0;
    return StringParseStatus_Success;
}

// Status of a strto* call that stopped at endptr: the whole buffer has to be consumed, and errno carries the range
// error, which the caller names in range_status.
static StringParseStatus parse_end(const char* buffer, const char* endptr, StringParseStatus range_status) {
    if (endptr == buffer || *endptr != '\0') {
        return StringParseStatus_InvalidFormat;
    }

    return errno == ERANGE ? range_status : StringParseStatus_Success;
}

static StringParseStatus range_status(bool is_above, bool is_below) {
    if (is_above) {
        return StringParseStatus_Overflow;
    }
    return is_below ? StringParseStatus_Underflow : StringParseStatus_Success;
}

// The whitespace strto* skips before a number
static bool is_c_space(char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

static bool has_sign(const char* cstr) {
    while (is_c_space(*cstr)) {
        ++cstr;
    }
    return *cstr == '-' || *cstr == '+';
}

StringParseResultU64 fl_string_parse_u64(const FlString str) {
    char buffer[32];
    StringParseStatus status = parse_begin(str, buffer, sizeof(buffer));

    // strtoull silently accepts a sign after optional whitespace and wraps negative values into huge
    // unsigned results; an unsigned parse must reject signed input outright.
    if (status == StringParseStatus_Success && has_sign(buffer)) {
        status = StringParseStatus_InvalidFormat;
    }

    u64 value = 0;
    if (status == StringParseStatus_Success) {
        char* endptr;
        value = strtoull(buffer, &endptr, 10);
        status = parse_end(buffer, endptr, StringParseStatus_Overflow);
    }

    return (StringParseResultU64) { status == StringParseStatus_Success ? value : 0, status };
}

StringParseResultI64 fl_string_parse_i64(const FlString str) {
    char buffer[32];
    const StringParseStatus begin_status = parse_begin(str, buffer, sizeof(buffer));
    if (begin_status != StringParseStatus_Success) {
        return (StringParseResultI64) { 0, begin_status };
    }

    char* endptr;
    const i64 value = strtoll(buffer, &endptr, 10);
    // On a range error strtoll returns the limit that was crossed
    const StringParseStatus status = parse_end(buffer, endptr, range_status(value == LLONG_MAX, true));

    return (StringParseResultI64) { status == StringParseStatus_Success ? value : 0, status };
}

// On a range error strtod returns +-HUGE_VAL for overflow and a value near zero for underflow
static StringParseStatus f64_range_status(double value) {
    return range_status(fabs(value) == HUGE_VAL, true);
}

StringParseResultF64 fl_string_parse_f64(const FlString str) {
    char buffer[64];
    char* endptr = buffer;
    double value = 0.0;

    StringParseStatus status = parse_begin(str, buffer, sizeof(buffer));
    if (status == StringParseStatus_Success) {
        value = strtod(buffer, &endptr);
        status = parse_end(buffer, endptr, f64_range_status(value));
    }

    if (status != StringParseStatus_Success) {
        value = 0.0;
    }
    return (StringParseResultF64) { value, status };
}

static StringParseResultU32 narrow_to_u32(const StringParseResultU64 wide) {
    StringParseStatus status = wide.status;
    if (status == StringParseStatus_Success) {
        status = range_status(wide.value > UINT32_MAX, false);
    }

    return (StringParseResultU32) { status == StringParseStatus_Success ? (u32)wide.value : 0, status };
}

StringParseResultU32 fl_string_parse_u32(const FlString str) {
    return narrow_to_u32(fl_string_parse_u64(str));
}

StringParseResultI32 fl_string_parse_i32(const FlString str) {
    const StringParseResultI64 wide = fl_string_parse_i64(str);
    if (wide.status != StringParseStatus_Success) {
        return (StringParseResultI32) { 0, wide.status };
    }

    const StringParseStatus status = range_status(wide.value > INT32_MAX, wide.value < INT32_MIN);
    return (StringParseResultI32) { status == StringParseStatus_Success ? (i32)wide.value : 0, status };
}

StringParseResultF32 fl_string_parse_f32(const FlString str) {
    StringParseResultF32 result = { 0.0f, StringParseStatus_Success };

    const StringParseResultF64 wide = fl_string_parse_f64(str);
    result.status = wide.status;
    if (result.status == StringParseStatus_Success) {
        result.status = range_status(wide.value > FLT_MAX, wide.value < -FLT_MAX);
    }

    if (result.status == StringParseStatus_Success) {
        result.value = (f32)wide.value;
    }
    return result;
}

// Value of a hex digit, or -1 when c is not one
static int hex_digit_value(char c) {
    if (is_digit(c)) {
        return c - '0';
    }

    const char lower = char_to_lower(c);
    if (lower >= 'a' && lower <= 'f') {
        return lower - 'a' + 10;
    }

    return -1;
}

static bool has_hex_prefix(const FlString str) {
    return str.length >= 2 && str.data[0] == '0' && char_to_lower(str.data[1]) == 'x';
}

StringParseResultU64 fl_string_parse_hex_u64(const FlString str) {
    if (string_is_empty(str)) {
        return (StringParseResultU64) { 0, StringParseStatus_Empty };
    }

    const FlString digits = has_hex_prefix(str) ? fl_string_substr(str, 2, str.length - 2) : str;
    if (digits.length == 0) {
        return (StringParseResultU64) { 0, StringParseStatus_InvalidHexPrefix };
    }

    u64 result = 0;
    for (u64 i = 0; i < digits.length; i++) {
        const int digit = hex_digit_value(digits.data[i]);
        if (digit < 0) {
            return (StringParseResultU64) { 0, StringParseStatus_InvalidFormat };
        }

        // Check for overflow before multiplying
        if (result > (UINT64_MAX >> 4)) {
            return (StringParseResultU64) { 0, StringParseStatus_Overflow };
        }

        result = result * 16 + (u64)digit;
    }

    return (StringParseResultU64) { result, StringParseStatus_Success };
}

StringParseResultU32 fl_string_parse_hex_u32(const FlString str) {
    return narrow_to_u32(fl_string_parse_hex_u64(str));
}

StringParseResultU64 fl_string_parse_number_u64(const FlString str) {
    if (string_is_empty(str)) {
        return (StringParseResultU64) { 0, StringParseStatus_Empty };
    }

    return has_hex_prefix(str) ? fl_string_parse_hex_u64(str) : fl_string_parse_u64(str);
}

StringParseResultU32 fl_string_parse_number_u32(const FlString str) {
    return narrow_to_u32(fl_string_parse_number_u64(str));
}

// FlString search and URL utilities

i64 fl_string_find_char(const FlString str, char c) {
    for (u64 i = 0; i < str.length; i++) {
        if (str.data[i] == c) {
            return (i64)i;
        }
    }
    return -1;
}

typedef bool (*BytesEqualFn)(const char* a, const char* b, u64 length);

static bool bytes_equal(const char* a, const char* b, u64 length) {
    return memory_compare(a, b, length) == 0;
}

// Index of the first place in str where needle matches under equal, or -1. An empty needle matches at 0.
static i64 find_substr_with(const FlString str, const FlString needle, BytesEqualFn equal) {
    if (needle.length == 0) {
        return 0;
    }
    if (needle.length > str.length) {
        return -1;
    }

    const u64 last_start = str.length - needle.length;
    for (u64 i = 0; i <= last_start; i++) {
        if (equal(str.data + i, needle.data, needle.length)) {
            return (i64)i;
        }
    }
    return -1;
}

i64 fl_string_find_substr(const FlString str, const FlString needle) {
    return find_substr_with(str, needle, bytes_equal);
}

i64 fl_string_find_char_from(const FlString str, char c, u64 offset) {
    if (offset >= str.length) {
        return -1;
    }

    for (u64 i = offset; i < str.length; i++) {
        if (str.data[i] == c) {
            return (i64)i;
        }
    }
    return -1;
}

bool fl_string_contains(const FlString str, const FlString needle) {
    return fl_string_find_substr(str, needle) != -1;
}

// Case-insensitive for ASCII only
bool fl_string_contains_nocase(const FlString str, const FlString needle) {
    return find_substr_with(str, needle, bytes_equal_nocase) != -1;
}

#define FNV1A_64_INIT 0xcbf29ce484222325ULL
#define FNV1A_64_PRIME 0x100000001b3ULL

u64 fl_string_hash_fnv1a(const FlString str) {
    u64 hash = FNV1A_64_INIT;
    for (u64 i = 0; i < str.length; i++) {
        hash ^= (u64)(u8)str.data[i];
        hash *= FNV1A_64_PRIME;
    }
    return hash;
}
