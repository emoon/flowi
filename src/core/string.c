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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* fl_string_to_cstr(struct FlArena* arena, const FlString str) {
    char* temp = arena_alloc_array(arena, char, str.length + 1);
    memory_copy(temp, str.length + 1, str.data, str.length);
    temp[str.length] = '\0';
    return temp;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_string_to_cstr_buffer(char* dest, const u64 buffer_len, const FlString str) {
    if (buffer_len == 0) {
        return; // No room for anything, not even the null terminator
    }
    const u64 copy_len = str.length < buffer_len - 1 ? str.length : buffer_len - 1;
    memory_copy(dest, buffer_len, str.data, copy_len);
    dest[copy_len] = '\0';
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_string_begins_with(const FlString str, const FlString needle) {
    if (needle.length > str.length) {
        return false;
    }
    return memory_compare(str.data, needle.data, needle.length) == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_string_equals(const FlString a, const FlString b) {
    if (a.length != b.length) {
        return false;
    }

    if (a.length == 0) {
        return true;
    }

    if (a.is_static && b.is_static && a.data == b.data) {
        return true;
    }

    return memory_compare(a.data, b.data, a.length) == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline char char_to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_string_equals_nocase(const FlString a, const FlString b) {
    if (a.length != b.length) {
        return false;
    }

    if (a.length == 0) {
        return true;
    }

    if (a.is_static && b.is_static && a.data == b.data) {
        return true;
    }

    for (u64 i = 0; i < a.length; i++) {
        if (char_to_lower(a.data[i]) != char_to_lower(b.data[i])) {
            return false;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int fl_string_compare_n(const FlString a, const FlString b, u64 max_len) {
    u64 compare_len = a.length < b.length ? a.length : b.length;
    // The length tie-break below is only meaningful when the full common prefix was compared; when max_len
    // cuts the comparison short, equal bytes within the bound mean "equal within the bound" - return 0.
    bool was_clamped = max_len < compare_len;
    if (was_clamped) {
        compare_len = max_len;
    }
    if (compare_len == 0) {
        if (was_clamped) {
            return 0;
        }
        return a.length == b.length ? 0 : (a.length < b.length ? -1 : 1);
    }
    int result = memory_compare(a.data, b.data, compare_len);
    if (result == 0 && !was_clamped && a.length != b.length) {
        return a.length < b.length ? -1 : 1;
    }
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Natural string comparison (case-insensitive, with numeric awareness)
// Compares strings in a human-friendly way: "file1" < "file2" < "file10"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int fl_string_compare_natural(const FlString a, const FlString b) {
    u64 i = 0, j = 0;

    while (i < a.length && j < b.length) {
        char ca = a.data[i];
        char cb = b.data[j];

        if (is_digit(ca) && is_digit(cb)) {
            // Skip leading zeros
            while (i < a.length && a.data[i] == '0')
                i++;
            while (j < b.length && b.data[j] == '0')
                j++;

            u64 num_start_a = i;
            u64 num_start_b = j;

            while (i < a.length && is_digit(a.data[i]))
                i++;
            while (j < b.length && is_digit(b.data[j]))
                j++;

            u64 num_len_a = i - num_start_a;
            u64 num_len_b = j - num_start_b;

            // Compare by length first (longer number is bigger)
            if (num_len_a != num_len_b) {
                return num_len_a < num_len_b ? -1 : 1;
            }

            for_count(k, num_len_a) {
                if (a.data[num_start_a + k] != b.data[num_start_b + k]) {
                    return a.data[num_start_a + k] < b.data[num_start_b + k] ? -1 : 1;
                }
            }
        } else {
            char lower_a = to_lower(ca);
            char lower_b = to_lower(cb);

            if (lower_a != lower_b) {
                return lower_a < lower_b ? -1 : 1;
            }

            i++;
            j++;
        }
    }

    // If we've exhausted one string, the shorter one comes first
    if (i < a.length)
        return 1;
    if (j < b.length)
        return -1;
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_string_concat(struct FlArena* arena, FlString s0, FlString s1) {
    const u64 total_length = s0.length + s1.length;
    char* buffer = arena_alloc_array(arena, char, total_length);

    memory_copy(buffer, total_length, s0.data, s0.length);
    memory_copy(buffer + s0.length, total_length - s0.length, s1.data, s1.length);

    return (FlString) { .data = buffer, .length = total_length, .is_ascii = s0.is_ascii && s1.is_ascii };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline FlString string_copy_impl(char* buffer, const FlString str) {
    memory_copy(buffer, str.length, str.data, str.length);
    return (FlString) { .data = buffer, .length = str.length, .is_ascii = str.is_ascii };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_string_copy(struct FlArena* arena, const FlString str) {
    // Static strings don't need to be copied
    if (str.is_static) {
        return str;
    }

    if (str.length == 0) {
        return string_empty();
    }

    char* buffer = arena_alloc_array(arena, char, str.length);
    return string_copy_impl(buffer, str);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_string_copy_malloc(const FlString str) {
    // Static strings don't need to be copied
    if (str.is_static) {
        return str;
    }

    if (str.length == 0) {
        return string_empty();
    }

    char* buffer = mi_malloc(str.length);
    memory_copy(buffer, str.length, str.data, str.length);

    return (FlString) { .data = buffer, .length = str.length, .is_ascii = str.is_ascii };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_string_to_lower(struct FlArena* arena, const FlString str) {
    if (str.length == 0) {
        return string_empty();
    }

    char* buffer = arena_alloc_array(arena, char, str.length);
    for (u64 i = 0; i < str.length; i++) {
        buffer[i] = to_lower(str.data[i]);
    }

    return (FlString) { .data = buffer, .length = str.length, .is_ascii = str.is_ascii };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_string_free(FlString str) {
    // Only free strings that were dynamically allocated
    // Don't free: static strings, empty strings (which point to ""), or nullptr
    if (str.length > 0 && !str.is_static && str.data) {
        mi_free((void*)str.data);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// FlStringBuilder implementation

FlStringBuilder fl_sb_create(struct FlArena* arena) {
    return (FlStringBuilder) { .arena = arena, .start_pos = arena->pos };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlStringBuilder fl_sb_append_cstr(FlStringBuilder sb, const char* str) {
    if (!str)
        return sb;

    const u64 str_len = strlen(str);
    char* dest = arena_alloc_array(sb.arena, char, str_len);
    memory_copy(dest, str_len, str, str_len);

    return sb;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlStringBuilder fl_sb_append_string(FlStringBuilder sb, FlString str) {
    if (str.length == 0)
        return sb;

    char* dest = arena_alloc_array(sb.arena, char, str.length);
    memory_copy(dest, str.length, str.data, str.length);

    return sb;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlStringBuilder fl_sb_append_char(FlStringBuilder sb, char c) {
    char* dest = fl_arena_alloc_raw(sb.arena, 1, 1);
    *dest = c;

    return sb;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlStringBuilder fl_sb_appendf(FlStringBuilder sb, const char* format, ...) {
    va_list args;
    va_start(args, format);
    FlStringBuilder result = sb_vappendf(sb, format, args);
    va_end(args);
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlStringBuilder sb_vappendf(FlStringBuilder sb, const char* format, va_list args) {
    vsprintf_arena(sb.arena, format, args);
    return sb;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlStringBuilder fl_sb_clear(FlStringBuilder sb) {
    sb.arena->pos = sb.start_pos;
    return sb;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_sb_to_string(FlStringBuilder sb) {
    const u64 length = sb.arena->pos - sb.start_pos;
    const char* data = (const char*)sb.arena->ptr + sb.start_pos;
    // Conservative: FlStringBuilder does not track ASCII status.
    return (FlString) { .data = data, .length = length, .is_ascii = 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* fl_sb_to_cstr(FlStringBuilder sb) {
    fl_arena_alloc_raw(sb.arena, 1, 1);
    char* null_term = (char*)sb.arena->ptr + sb.arena->pos - 1;
    *null_term = '\0';

    const char* data = (const char*)sb.arena->ptr + sb.start_pos;
    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 fl_sb_length(FlStringBuilder sb) {
    return sb.arena->pos - sb.start_pos;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_string_ends_with(const FlString str, const FlString needle) {
    if (needle.length > str.length) {
        return false;
    }

    const char* str_end = str.data + str.length - needle.length;
    return memory_compare(str_end, needle.data, needle.length) == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char* string_to_null_terminated(const FlString str, char* buffer, u64 buffer_size) {
    if (str.length == 0) {
        buffer[0] = '\0';
        return buffer;
    }

    if (str.length >= buffer_size) {
        return nullptr; // FlString too long
    }

    memory_copy(buffer, buffer_size, str.data, str.length);
    buffer[str.length] = '\0';
    return buffer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 hex_digit_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return 0; // Should never reach here if is_hex_digit was checked
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StringParseResultU64 fl_string_parse_u64(const FlString str) {
    if (str.length == 0 || !str.data) {
        return (StringParseResultU64) { 0, StringParseStatus_Empty };
    }

    char buffer[32];
    if (!string_to_null_terminated(str, buffer, sizeof(buffer))) {
        return (StringParseResultU64) { 0, StringParseStatus_InvalidFormat };
    }

    // strtoull silently accepts a sign after optional whitespace and wraps negative values into huge
    // unsigned results; an unsigned parse must reject signed input outright.
    const char* first = buffer;
    while (*first == ' ' || *first == '\t' || *first == '\n' || *first == '\v' || *first == '\f' || *first == '\r') {
        ++first;
    }
    if (*first == '-' || *first == '+') {
        return (StringParseResultU64) { 0, StringParseStatus_InvalidFormat };
    }

    errno = 0;
    char* endptr;
    unsigned long long result = strtoull(buffer, &endptr, 10);

    if (endptr == buffer) {
        return (StringParseResultU64) { 0, StringParseStatus_InvalidFormat };
    }

    if (*endptr != '\0') {
        return (StringParseResultU64) { 0, StringParseStatus_InvalidFormat };
    }

    if (errno == ERANGE) {
        return (StringParseResultU64) { 0, StringParseStatus_Overflow };
    }

    return (StringParseResultU64) { (u64)result, StringParseStatus_Success };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_string_substr(const FlString str, u64 start, u64 length) {
    if (start >= str.length) {
        return string_empty();
    }

    u64 actual_length = length;
    if (start + length > str.length) {
        actual_length = str.length - start;
    }

    // Substring of ASCII string is ASCII
    return (FlString) { .data = str.data + start, .length = actual_length, .is_ascii = str.is_ascii };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StringParseResultI64 fl_string_parse_i64(const FlString str) {
    if (str.length == 0 || !str.data) {
        return (StringParseResultI64) { 0, StringParseStatus_Empty };
    }

    char buffer[32];
    if (!string_to_null_terminated(str, buffer, sizeof(buffer))) {
        return (StringParseResultI64) { 0, StringParseStatus_InvalidFormat };
    }

    errno = 0;
    char* endptr;
    long long result = strtoll(buffer, &endptr, 10);

    if (endptr == buffer) {
        return (StringParseResultI64) { 0, StringParseStatus_InvalidFormat };
    }

    if (*endptr != '\0') {
        return (StringParseResultI64) { 0, StringParseStatus_InvalidFormat };
    }

    if (errno == ERANGE) {
        if (result == LLONG_MAX) {
            return (StringParseResultI64) { 0, StringParseStatus_Overflow };
        } else {
            return (StringParseResultI64) { 0, StringParseStatus_Underflow };
        }
    }

    return (StringParseResultI64) { (i64)result, StringParseStatus_Success };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StringParseResultU32 fl_string_parse_u32(const FlString str) {
    StringParseResultU64 result = fl_string_parse_u64(str);

    if (result.status != StringParseStatus_Success) {
        return (StringParseResultU32) { 0, result.status };
    }

    if (result.value > UINT32_MAX) {
        return (StringParseResultU32) { 0, StringParseStatus_Overflow };
    }

    return (StringParseResultU32) { (u32)result.value, StringParseStatus_Success };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StringParseResultI32 fl_string_parse_i32(const FlString str) {
    StringParseResultI64 result = fl_string_parse_i64(str);

    if (result.status != StringParseStatus_Success) {
        return (StringParseResultI32) { 0, result.status };
    }

    if (result.value > INT32_MAX) {
        return (StringParseResultI32) { 0, StringParseStatus_Overflow };
    }

    if (result.value < INT32_MIN) {
        return (StringParseResultI32) { 0, StringParseStatus_Underflow };
    }

    return (StringParseResultI32) { (i32)result.value, StringParseStatus_Success };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StringParseResultF64 fl_string_parse_f64(const FlString str) {
    if (str.length == 0 || !str.data) {
        return (StringParseResultF64) { 0.0, StringParseStatus_Empty };
    }

    char buffer[64];
    if (!string_to_null_terminated(str, buffer, sizeof(buffer))) {
        return (StringParseResultF64) { 0.0, StringParseStatus_InvalidFormat };
    }

    errno = 0;
    char* endptr;
    double result = strtod(buffer, &endptr);

    if (endptr == buffer) {
        return (StringParseResultF64) { 0.0, StringParseStatus_InvalidFormat };
    }

    if (*endptr != '\0') {
        return (StringParseResultF64) { 0.0, StringParseStatus_InvalidFormat };
    }

    if (errno == ERANGE) {
        if (result == HUGE_VAL || result == -HUGE_VAL) {
            return (StringParseResultF64) { 0.0, StringParseStatus_Overflow };
        } else {
            return (StringParseResultF64) { 0.0, StringParseStatus_Underflow };
        }
    }

    return (StringParseResultF64) { result, StringParseStatus_Success };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StringParseResultF32 fl_string_parse_f32(const FlString str) {
    StringParseResultF64 result = fl_string_parse_f64(str);

    if (result.status != StringParseStatus_Success) {
        return (StringParseResultF32) { 0.0f, result.status };
    }

    if (result.value > FLT_MAX) {
        return (StringParseResultF32) { 0.0f, StringParseStatus_Overflow };
    }

    if (result.value < -FLT_MAX) {
        return (StringParseResultF32) { 0.0f, StringParseStatus_Underflow };
    }

    return (StringParseResultF32) { (f32)result.value, StringParseStatus_Success };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StringParseResultU64 fl_string_parse_hex_u64(const FlString str) {
    if (str.length == 0 || !str.data) {
        return (StringParseResultU64) { 0, StringParseStatus_Empty };
    }

    u64 start_idx = 0;

    if (str.length >= 2 && str.data[0] == '0' && (str.data[1] == 'x' || str.data[1] == 'X')) {
        start_idx = 2;
    }

    if (start_idx >= str.length) {
        return (StringParseResultU64) { 0, StringParseStatus_InvalidHexPrefix };
    }

    u64 result = 0;
    for (u64 i = start_idx; i < str.length; i++) {
        char c = str.data[i];

        if (!is_hex_digit(c)) {
            return (StringParseResultU64) { 0, StringParseStatus_InvalidFormat };
        }

        // Check for overflow before multiplying
        if (result > (UINT64_MAX >> 4)) {
            return (StringParseResultU64) { 0, StringParseStatus_Overflow };
        }

        result = result * 16 + hex_digit_value(c);
    }

    return (StringParseResultU64) { result, StringParseStatus_Success };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StringParseResultU32 fl_string_parse_hex_u32(const FlString str) {
    StringParseResultU64 result = fl_string_parse_hex_u64(str);

    if (result.status != StringParseStatus_Success) {
        return (StringParseResultU32) { 0, result.status };
    }

    if (result.value > UINT32_MAX) {
        return (StringParseResultU32) { 0, StringParseStatus_Overflow };
    }

    return (StringParseResultU32) { (u32)result.value, StringParseStatus_Success };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StringParseResultU64 fl_string_parse_number_u64(const FlString str) {
    if (str.length == 0 || !str.data) {
        return (StringParseResultU64) { 0, StringParseStatus_Empty };
    }

    if (str.length >= 2 && str.data[0] == '0' && (str.data[1] == 'x' || str.data[1] == 'X')) {
        return fl_string_parse_hex_u64(str);
    }

    return fl_string_parse_u64(str);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StringParseResultU32 fl_string_parse_number_u32(const FlString str) {
    StringParseResultU64 result = fl_string_parse_number_u64(str);

    if (result.status != StringParseStatus_Success) {
        return (StringParseResultU32) { 0, result.status };
    }

    if (result.value > UINT32_MAX) {
        return (StringParseResultU32) { 0, StringParseStatus_Overflow };
    }

    return (StringParseResultU32) { (u32)result.value, StringParseStatus_Success };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FlString search and URL utilities

i64 fl_string_find_char(const FlString str, char c) {
    for (u64 i = 0; i < str.length; i++) {
        if (str.data[i] == c) {
            return (i64)i;
        }
    }
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 fl_string_find_substr(const FlString str, const FlString needle) {
    if (needle.length == 0) {
        return 0; // Empty needle found at position 0
    }
    if (needle.length > str.length) {
        return -1;
    }

    for (u64 i = 0; i <= str.length - needle.length; i++) {
        bool match = true;
        for (u64 j = 0; j < needle.length; j++) {
            if (str.data[i + j] != needle.data[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            return (i64)i;
        }
    }
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_string_contains(const FlString str, const FlString needle) {
    return fl_string_find_substr(str, needle) != -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_string_contains_nocase(const FlString str, const FlString needle) {
    if (needle.length == 0) {
        return true;
    }
    if (str.length < needle.length) {
        return false;
    }

    for (u64 i = 0; i <= str.length - needle.length; i++) {
        bool match = true;
        for (u64 j = 0; j < needle.length; j++) {
            char s = str.data[i + j];
            char n = needle.data[j];
            // Convert to lowercase for comparison (ASCII only)
            if (s >= 'A' && s <= 'Z')
                s += 32;
            if (n >= 'A' && n <= 'Z')
                n += 32;
            if (s != n) {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
