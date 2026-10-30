/*
   The latest version of this library is available on GitHub;
   https://github.com/sheredom/json.h.

   This code has been adapted to fit the codebase style.
*/

/*
   This is free and unencumbered software released into the public domain.

   Anyone is free to copy, modify, publish, use, compile, sell, or
   distribute this software, either in source code form or as a compiled
   binary, for any purpose, commercial or non-commercial, and by any
   means.

   In jurisdictions that recognize copyright laws, the author or authors
   of this software dedicate any and all copyright interest in the
   software to the public domain. We make this dedication for the benefit
   of the public at large and to the detriment of our heirs and
   successors. We intend this dedication to be an overt act of
   relinquishment in perpetuity of all present and future rights to this
   software under copyright law.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
   OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
   ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
   OTHER DEALINGS IN THE SOFTWARE.

   For more information, please refer to <http://unlicense.org/>.
*/

#include "json.h"
#include "arena.h"
#include "core.h"
#include "string.h"
#include "memory.h"
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* A JSON string value (extended). */
typedef struct JsonStringEx {
    /* The JSON string this extends. */
    FlString string;

    /* The character offset for the value in the JSON input. */
    size_t offset;

    /* The line number for the value in the JSON input. */
    size_t line_no;

    /* The row number for the value in the JSON input. */
    size_t row_no;
} JsonStringEx;

/* A JSON value (extended). Every value node is one of these; the location fields are zero when
 * nothing recorded them. */
typedef struct JsonValueEx {
    /* the JSON value this extends. */
    struct FlJsonValue value;

    /* the character offset for the value in the JSON input. */
    size_t offset;

    /* the line number for the value in the JSON input. */
    size_t line_no;

    /* the row number for the value in the JSON input. */
    size_t row_no;

    /* pointer to the start of the line in the original JSON source. */
    const char* line_start;

    /* pointer to the end of the line in the original JSON source. */
    const char* line_end;
} JsonValueEx;

#define json_weak __inline

#include <inttypes.h>
#define json_uintmax_t uintmax_t

#if COMPILER_MSVC
#define json_strtoumax _strtoui64
#else
#define json_strtoumax strtoumax
#endif

#define json_null 0

#if COMPILER_CLANG
#pragma clang diagnostic push

/* we do one big allocation via malloc, then cast aligned slices of this for. */
/* our structures - we don't have a way to tell the compiler we know what we. */
/* are doing, so disable the warning instead! */
#pragma clang diagnostic ignored "-Wcast-align"

/* We use C style casts everywhere. */
#pragma clang diagnostic ignored "-Wold-style-cast"

/* We need long long for strtoull. */
#pragma clang diagnostic ignored "-Wc++11-long-long"

#if __has_warning("-Wunsafe-buffer-usage")
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif

#elif COMPILER_MSVC
#pragma warning(push)

/* disable 'function selected for inline expansion' warning. */
#pragma warning(disable : 4711)

/* disable '#pragma warning: there is no warning number' warning. */
#pragma warning(disable : 4619)

/* disable 'warning number not a valid compiler warning' warning. */
#pragma warning(disable : 4616)

/* disable 'Compiler will insert Spectre mitigation for memory load if
 * /Qspectre. */
/* switch specified' warning. */
#pragma warning(disable : 5045)
#endif

/* Maximum object/array nesting depth accepted from input. */
#define JSON_PARSE_MAX_DEPTH 256

struct json_parse_state_s {
    const char* src;
    size_t size;
    size_t offset;
    size_t flags_bitset;
    char* data;
    char* dom;
    size_t dom_size;
    size_t data_size;
    size_t line_no;     /* line counter for error reporting. */
    size_t line_offset; /* (offset-line_offset) is the character number (in
                           bytes). */
    size_t depth;       /* current object/array nesting depth. */
    size_t error;
};

/* Callers pair a successful push with a manual depth decrement. */
static int json_depth_push(struct json_parse_state_s* state) {
    if (state->depth >= JSON_PARSE_MAX_DEPTH) {
        state->error = FlJsonParseError_TooDeep;
        return 1;
    }
    state->depth++;
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_set_line_boundaries(struct JsonValueEx* value_ex, const struct json_parse_state_s* state) {
    // line_offset points to the newline that ended the previous line, so skip past it
    // to get to the actual start of this line
    const char* line_start = state->src + state->line_offset;

    if (state->line_offset > 0) {
        // Skip \n or \r\n
        if (*line_start == '\n') {
            line_start++;
        } else if (*line_start == '\r') {
            line_start++;
            if (line_start < state->src + state->size && *line_start == '\n') {
                line_start++;
            }
        }
    }

    value_ex->line_start = line_start;

    const char* line_end = state->src + state->size;
    for (const char* p = value_ex->line_start; p < state->src + state->size; p++) {
        if (*p == '\n' || (*p == '\r' && (p + 1 >= state->src + state->size || *(p + 1) != '\n'))) {
            line_end = p;
            break;
        } else if (*p == '\r' && *(p + 1) == '\n') {
            line_end = p;
            break;
        }
    }
    value_ex->line_end = line_end;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Carve the next value node out of the DOM block, recording where it started when the parse was
// asked for location information.

static struct FlJsonValue* json_alloc_dom_value(struct json_parse_state_s* state) {
    struct JsonValueEx* value_ex = (struct JsonValueEx*)state->dom;
    state->dom += sizeof(struct JsonValueEx);

    if (FlJsonParseFlags_AllowLocationInformation & state->flags_bitset) {
        value_ex->offset = state->offset;
        value_ex->line_no = state->line_no;
        value_ex->row_no = state->offset - state->line_offset;
        json_set_line_boundaries(value_ex, state);
    } else {
        *value_ex = (struct JsonValueEx) { 0 };
    }

    return &value_ex->value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlJsonValue* json_value_alloc_zero(FlArena* arena) {
    struct JsonValueEx* value_ex = arena_alloc_zero(arena, struct JsonValueEx);
    return &value_ex->value;
}

json_weak int json_hexadecimal_digit(const char c);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_hexadecimal_digit(const char c) {
    if ('0' <= c && c <= '9') {
        return c - '0';
    }
    if ('a' <= c && c <= 'f') {
        return c - 'a' + 10;
    }
    if ('A' <= c && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

json_weak int json_hexadecimal_value(const char* c, const unsigned long size, unsigned long* result);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_hexadecimal_value(const char* c, const unsigned long size, unsigned long* result) {
    const char* p;
    int digit;

    if (size > sizeof(unsigned long) * 2) {
        return 0;
    }

    *result = 0;
    for (p = c; (unsigned long)(p - c) < size; ++p) {
        *result <<= 4;
        digit = json_hexadecimal_digit(*p);
        if (digit < 0 || digit > 15) {
            return 0;
        }
        *result |= (unsigned char)digit;
    }
    return 1;
}

json_weak int json_skip_whitespace(struct json_parse_state_s* state);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_skip_whitespace(struct json_parse_state_s* state) {
    size_t offset = state->offset;
    const size_t size = state->size;
    const char* const src = state->src;

    if (offset >= state->size) {
        return 0;
    }

    /* the only valid whitespace according to ECMA-404 is ' ', '\n', '\r' and
     * '\t'. */
    switch (src[offset]) {
        default:
            return 0;
        case ' ':
        case '\r':
        case '\t':
        case '\n':
            break;
    }

    do {
        switch (src[offset]) {
            default:
                /* Update offset. */
                state->offset = offset;
                return 1;
            case ' ':
            case '\r':
            case '\t':
                break;
            case '\n':
                state->line_no++;
                state->line_offset = offset;
                break;
        }

        offset++;
    } while (offset < size);

    /* Update offset. */
    state->offset = offset;
    return 1;
}

json_weak int json_skip_c_style_comments(struct json_parse_state_s* state);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_skip_c_style_comments(struct json_parse_state_s* state) {
    /* to have a C-style comment we need at least 2 characters of space */
    if ((state->offset + 2) > state->size) {
        return 0;
    }

    /* do we have a comment? */
    if ('/' == state->src[state->offset]) {
        if ('/' == state->src[state->offset + 1]) {
            /* we had a comment of the form // */

            /* skip first '/' */
            state->offset++;

            /* skip second '/' */
            state->offset++;

            while (state->offset < state->size) {
                switch (state->src[state->offset]) {
                    default:
                        /* skip the character in the comment */
                        state->offset++;
                        break;
                    case '\n':
                        /* if we have a newline, our comment has ended! Skip the newline */
                        state->offset++;

                        /* we entered a newline, so move our line info forward */
                        state->line_no++;
                        state->line_offset = state->offset;
                        return 1;
                }
            }

            /* we reached the end of the JSON file! */
            return 1;
        } else if ('*' == state->src[state->offset + 1]) {
            /* we had a comment in the C-style long form */

            /* skip '/' */
            state->offset++;

            /* skip '*' */
            state->offset++;

            while (state->offset + 1 < state->size) {
                if (('*' == state->src[state->offset]) && ('/' == state->src[state->offset + 1])) {
                    /* we reached the end of our comment! */
                    state->offset += 2;
                    return 1;
                } else if ('\n' == state->src[state->offset]) {
                    /* we entered a newline, so move our line info forward */
                    state->line_no++;
                    state->line_offset = state->offset;
                }

                /* skip character within comment */
                state->offset++;
            }

            /* comment wasn't ended correctly which is a failure */
            return 1;
        }
    }

    /* we didn't have any comment, which is ok too! */
    return 0;
}

json_weak int json_skip_all_skippables(struct json_parse_state_s* state);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_skip_all_skippables(struct json_parse_state_s* state) {
    /* skip all whitespace and other skippables until there are none left. */

    int did_consume = 0;
    const size_t size = state->size;

    if (FlJsonParseFlags_AllowCStyleComments & state->flags_bitset) {
        do {
            if (state->offset == size) {
                state->error = FlJsonParseError_PrematureEndOfBuffer;
                return 1;
            }

            did_consume = json_skip_whitespace(state);

            /* This should really be checked on access, not in front of every call.
             */
            if (state->offset >= size) {
                state->error = FlJsonParseError_PrematureEndOfBuffer;
                return 1;
            }

            did_consume |= json_skip_c_style_comments(state);
        } while (0 != did_consume);
    } else {
        do {
            if (state->offset == size) {
                state->error = FlJsonParseError_PrematureEndOfBuffer;
                return 1;
            }

            did_consume = json_skip_whitespace(state);
        } while (0 != did_consume);
    }

    if (state->offset == size) {
        state->error = FlJsonParseError_PrematureEndOfBuffer;
        return 1;
    }

    return 0;
}

json_weak int json_get_value_size(struct json_parse_state_s* state, int is_global_object);

json_weak int json_get_string_size(struct json_parse_state_s* state, size_t is_key);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_get_string_size(struct json_parse_state_s* state, size_t is_key) {
    size_t offset = state->offset;
    const size_t size = state->size;
    size_t data_size = 0;
    const char* const src = state->src;
    const int is_single_quote = '\'' == src[offset];
    const char quote_to_use = is_single_quote ? '\'' : '"';
    const size_t flags_bitset = state->flags_bitset;
    unsigned long codepoint;
    unsigned long high_surrogate = 0;

    if ((FlJsonParseFlags_AllowLocationInformation & flags_bitset) != 0 && is_key != 0) {
        state->dom_size += sizeof(struct JsonStringEx);
    } else {
        state->dom_size += sizeof(FlString);
    }

    if ('"' != src[offset]) {
        /* if we are allowed single quoted strings check for that too. */
        if (!((FlJsonParseFlags_AllowSingleQuotedStrings & flags_bitset) && is_single_quote)) {
            state->error = FlJsonParseError_ExpectedOpeningQuote;
            state->offset = offset;
            return 1;
        }
    }

    /* skip leading '"' or '\''. */
    offset++;

    while ((offset < size) && (quote_to_use != src[offset])) {
        /* add space for the character. */
        data_size++;

        switch (src[offset]) {
            default:
                break;
            case '\0':
            case '\t':
                state->error = FlJsonParseError_InvalidString;
                state->offset = offset;
                return 1;
        }

        if ('\\' == src[offset]) {
            /* skip reverse solidus character. */
            offset++;

            if (offset == size) {
                state->error = FlJsonParseError_PrematureEndOfBuffer;
                state->offset = offset;
                return 1;
            }

            switch (src[offset]) {
                default:
                    state->error = FlJsonParseError_InvalidStringEscapeSequence;
                    state->offset = offset;
                    return 1;
                case '"':
                case '\\':
                case '/':
                case 'b':
                case 'f':
                case 'n':
                case 'r':
                case 't':
                    /* all valid characters! */
                    offset++;
                    break;
                case 'u':
                    if (!(offset + 5 < size)) {
                        /* invalid escaped unicode sequence! */
                        state->error = FlJsonParseError_InvalidStringEscapeSequence;
                        state->offset = offset;
                        return 1;
                    }

                    codepoint = 0;
                    if (!json_hexadecimal_value(&src[offset + 1], 4, &codepoint)) {
                        /* escaped unicode sequences must contain 4 hexadecimal digits! */
                        state->error = FlJsonParseError_InvalidStringEscapeSequence;
                        state->offset = offset;
                        return 1;
                    }

                    /* Valid sequence!
                     * see: https://en.wikipedia.org/wiki/UTF-8#Invalid_code_points.
                     *      1       7       U + 0000        U + 007F        0xxxxxxx.
                     *      2       11      U + 0080        U + 07FF        110xxxxx
                     * 10xxxxxx.
                     *      3       16      U + 0800        U + FFFF        1110xxxx
                     * 10xxxxxx        10xxxxxx.
                     *      4       21      U + 10000       U + 10FFFF      11110xxx
                     * 10xxxxxx        10xxxxxx        10xxxxxx.
                     * Note: the high and low surrogate halves used by UTF-16 (U+D800
                     * through U+DFFF) and code points not encodable by UTF-16 (those after
                     * U+10FFFF) are not legal Unicode values, and their UTF-8 encoding must
                     * be treated as an invalid byte sequence. */

                    if (high_surrogate != 0) {
                        /* we previously read the high half of the \uxxxx\uxxxx pair, so now
                         * we expect the low half. */
                        if (codepoint >= 0xdc00 && codepoint <= 0xdfff) { /* low surrogate range. */
                            data_size += 3;
                            high_surrogate = 0;
                        } else {
                            state->error = FlJsonParseError_InvalidStringEscapeSequence;
                            state->offset = offset;
                            return 1;
                        }
                    } else if (codepoint <= 0x7f) {
                        data_size += 0;
                    } else if (codepoint <= 0x7ff) {
                        data_size += 1;
                    } else if (codepoint >= 0xd800 && codepoint <= 0xdbff) { /* high surrogate range. */
                        /* The codepoint is the first half of a "utf-16 surrogate pair". so we
                         * need the other half for it to be valid: \uHHHH\uLLLL. */
                        if (offset + 11 > size || '\\' != src[offset + 5] || 'u' != src[offset + 6]) {
                            state->error = FlJsonParseError_InvalidStringEscapeSequence;
                            state->offset = offset;
                            return 1;
                        }
                        high_surrogate = codepoint;
                    } else if (codepoint >= 0xd800 && codepoint <= 0xdfff) { /* low surrogate range. */
                        /* we did not read the other half before. */
                        state->error = FlJsonParseError_InvalidStringEscapeSequence;
                        state->offset = offset;
                        return 1;
                    } else {
                        data_size += 2;
                    }
                    /* escaped codepoints after 0xffff are supported in json through utf-16
                     * surrogate pairs: \uD83D\uDD25 for U+1F525. */

                    offset += 5;
                    break;
            }
        } else if (('\r' == src[offset]) || ('\n' == src[offset])) {
            if (!(FlJsonParseFlags_AllowMultiLineStrings & flags_bitset)) {
                /* invalid escaped unicode sequence! */
                state->error = FlJsonParseError_InvalidStringEscapeSequence;
                state->offset = offset;
                return 1;
            }

            offset++;
        } else {
            /* skip character (valid part of sequence). */
            offset++;
        }
    }

    /* If the offset is equal to the size, we had a non-terminated string! */
    if (offset == size) {
        state->error = FlJsonParseError_PrematureEndOfBuffer;
        state->offset = offset - 1;
        return 1;
    }

    /* skip trailing '"' or '\''. */
    offset++;

    /* add enough space to store the string. */
    state->data_size += data_size;

    /* one more byte for null terminator ending the string! */
    state->data_size++;

    /* update offset. */
    state->offset = offset;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

json_weak int is_valid_unquoted_key_char(const char c);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int is_valid_unquoted_key_char(const char c) {
    return (('0' <= c && c <= '9') || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('_' == c));
}

json_weak int json_get_key_size(struct json_parse_state_s* state);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_get_key_size(struct json_parse_state_s* state) {
    const size_t flags_bitset = state->flags_bitset;

    if (FlJsonParseFlags_AllowUnquotedKeys & flags_bitset) {
        size_t offset = state->offset;
        const size_t size = state->size;
        const char* const src = state->src;
        size_t data_size = state->data_size;

        /* if we are allowing unquoted keys, first grok for a quote... */
        if ('"' == src[offset]) {
            /* ... if we got a comma, just parse the key as a string as normal. */
            return json_get_string_size(state, 1);
        } else if ((FlJsonParseFlags_AllowSingleQuotedStrings & flags_bitset) && ('\'' == src[offset])) {
            /* ... if we got a comma, just parse the key as a string as normal. */
            return json_get_string_size(state, 1);
        } else {
            while ((offset < size) && is_valid_unquoted_key_char(src[offset])) {
                offset++;
                data_size++;
            }

            /* one more byte for null terminator ending the string! */
            data_size++;

            if (FlJsonParseFlags_AllowLocationInformation & flags_bitset) {
                state->dom_size += sizeof(struct JsonStringEx);
            } else {
                state->dom_size += sizeof(FlString);
            }

            /* update offset. */
            state->offset = offset;

            /* update data_size. */
            state->data_size = data_size;

            return 0;
        }
    } else {
        /* we are only allowed to have quoted keys, so just parse a string! */
        return json_get_string_size(state, 1);
    }
}

json_weak int json_get_object_size(struct json_parse_state_s* state, int is_global_object);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_get_object_size(struct json_parse_state_s* state, int is_global_object) {
    const size_t flags_bitset = state->flags_bitset;
    const char* const src = state->src;
    const size_t size = state->size;
    size_t elements = 0;
    int allow_comma = 0;
    int found_closing_brace = 0;

    if (is_global_object) {
        /* if we found an opening '{' of an object, we actually have a normal JSON
         * object at the root of the DOM... */
        if (!json_skip_all_skippables(state) && '{' == state->src[state->offset]) {
            /* . and we don't actually have a global object after all! */
            is_global_object = 0;
        }
    }

    if (!is_global_object) {
        if ('{' != src[state->offset]) {
            state->error = FlJsonParseError_Unknown;
            return 1;
        }

        /* skip leading '{'. */
        state->offset++;
    }

    state->dom_size += sizeof(struct FlJsonObject);

    if ((state->offset == size) && !is_global_object) {
        state->error = FlJsonParseError_PrematureEndOfBuffer;
        return 1;
    }

    do {
        if (!is_global_object) {
            if (json_skip_all_skippables(state)) {
                state->error = FlJsonParseError_PrematureEndOfBuffer;
                return 1;
            }

            if ('}' == src[state->offset]) {
                /* skip trailing '}'. */
                state->offset++;

                found_closing_brace = 1;

                /* finished the object! */
                break;
            }
        } else {
            /* we don't require brackets, so that means the object ends when the input
             * stream ends! */
            if (json_skip_all_skippables(state)) {
                break;
            }
        }

        /* if we parsed at least one element previously, grok for a comma. */
        if (allow_comma) {
            if (',' == src[state->offset]) {
                /* skip comma. */
                state->offset++;
                allow_comma = 0;
            } else if (FlJsonParseFlags_AllowNoCommas & flags_bitset) {
                /* we don't require a comma, and we didn't find one, which is ok! */
                allow_comma = 0;
            } else {
                /* otherwise we are required to have a comma, and we found none. */
                state->error = FlJsonParseError_ExpectedCommaOrClosingBracket;
                return 1;
            }

            if (FlJsonParseFlags_AllowTrailingComma & flags_bitset) {
                continue;
            } else {
                if (json_skip_all_skippables(state)) {
                    state->error = FlJsonParseError_PrematureEndOfBuffer;
                    return 1;
                }
            }
        }

        if (json_get_key_size(state)) {
            /* key parsing failed! */
            state->error = FlJsonParseError_InvalidString;
            return 1;
        }

        if (json_skip_all_skippables(state)) {
            state->error = FlJsonParseError_PrematureEndOfBuffer;
            return 1;
        }

        if (FlJsonParseFlags_AllowEqualsInObject & flags_bitset) {
            const char current = src[state->offset];
            if ((':' != current) && ('=' != current)) {
                state->error = FlJsonParseError_ExpectedColon;
                return 1;
            }
        } else {
            if (':' != src[state->offset]) {
                state->error = FlJsonParseError_ExpectedColon;
                return 1;
            }
        }

        /* skip colon. */
        state->offset++;

        if (json_skip_all_skippables(state)) {
            state->error = FlJsonParseError_PrematureEndOfBuffer;
            return 1;
        }

        if (json_get_value_size(state, /* is_global_object = */ 0)) {
            /* value parsing failed! */
            return 1;
        }

        /* successfully parsed a name/value pair! */
        elements++;
        allow_comma = 1;
    } while (state->offset < size);

    if ((state->offset == size) && !is_global_object && !found_closing_brace) {
        state->error = FlJsonParseError_PrematureEndOfBuffer;
        return 1;
    }

    state->dom_size += sizeof(struct FlJsonObjectElement) * elements;

    return 0;
}

json_weak int json_get_array_size(struct json_parse_state_s* state);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_get_array_size(struct json_parse_state_s* state) {
    const size_t flags_bitset = state->flags_bitset;
    size_t elements = 0;
    int allow_comma = 0;
    const char* const src = state->src;
    const size_t size = state->size;

    if ('[' != src[state->offset]) {
        /* expected array to begin with leading '['. */
        state->error = FlJsonParseError_Unknown;
        return 1;
    }

    /* skip leading '['. */
    state->offset++;

    state->dom_size += sizeof(struct FlJsonArray);

    while (state->offset < size) {
        if (json_skip_all_skippables(state)) {
            state->error = FlJsonParseError_PrematureEndOfBuffer;
            return 1;
        }

        if (']' == src[state->offset]) {
            /* skip trailing ']'. */
            state->offset++;

            state->dom_size += sizeof(struct FlJsonArrayElement) * elements;

            /* finished the object! */
            return 0;
        }

        /* if we parsed at least once element previously, grok for a comma. */
        if (allow_comma) {
            if (',' == src[state->offset]) {
                /* skip comma. */
                state->offset++;
                allow_comma = 0;
            } else if (!(FlJsonParseFlags_AllowNoCommas & flags_bitset)) {
                state->error = FlJsonParseError_ExpectedCommaOrClosingBracket;
                return 1;
            }

            if (FlJsonParseFlags_AllowTrailingComma & flags_bitset) {
                allow_comma = 0;
                continue;
            } else {
                if (json_skip_all_skippables(state)) {
                    state->error = FlJsonParseError_PrematureEndOfBuffer;
                    return 1;
                }
            }
        }

        if (json_get_value_size(state, /* is_global_object = */ 0)) {
            /* value parsing failed! */
            return 1;
        }

        /* successfully parsed an array element! */
        elements++;
        allow_comma = 1;
    }

    /* we consumed the entire input before finding the closing ']' of the array!
     */
    state->error = FlJsonParseError_PrematureEndOfBuffer;
    return 1;
}

json_weak int json_get_number_size(struct json_parse_state_s* state);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_get_number_size(struct json_parse_state_s* state) {
    const size_t flags_bitset = state->flags_bitset;
    size_t offset = state->offset;
    const size_t size = state->size;
    int had_leading_digits = 0;
    const char* const src = state->src;

    state->dom_size += sizeof(struct FlJsonNumber);

    if ((FlJsonParseFlags_AllowHexadecimalNumbers & flags_bitset) && (offset + 1 < size) && ('0' == src[offset])
        && (('x' == src[offset + 1]) || ('X' == src[offset + 1]))) {
        /* skip the leading 0x that identifies a hexadecimal number. */
        offset += 2;

        /* consume hexadecimal digits. */
        while ((offset < size)
               && (('0' <= src[offset] && src[offset] <= '9') || ('a' <= src[offset] && src[offset] <= 'f')
                   || ('A' <= src[offset] && src[offset] <= 'F'))) {
            offset++;
        }
    } else {
        int found_sign = 0;
        int inf_or_nan = 0;

        if ((offset < size)
            && (('-' == src[offset])
                || ((FlJsonParseFlags_AllowLeadingPlusSign & flags_bitset) && ('+' == src[offset])))) {
            /* skip valid leading '-' or '+'. */
            offset++;

            found_sign = 1;
        }

        if (FlJsonParseFlags_AllowInfAndNan & flags_bitset) {
            const char inf[9] = "Infinity";
            const size_t inf_strlen = sizeof(inf) - 1;
            const char nan[4] = "NaN";
            const size_t nan_strlen = sizeof(nan) - 1;

            if (offset + inf_strlen < size) {
                int found = 1;
                size_t i;
                for (i = 0; i < inf_strlen; i++) {
                    if (inf[i] != src[offset + i]) {
                        found = 0;
                        break;
                    }
                }

                if (found) {
                    /* We found our special 'Infinity' keyword! */
                    offset += inf_strlen;

                    inf_or_nan = 1;
                }
            }

            if (offset + nan_strlen < size) {
                int found = 1;
                size_t i;
                for (i = 0; i < nan_strlen; i++) {
                    if (nan[i] != src[offset + i]) {
                        found = 0;
                        break;
                    }
                }

                if (found) {
                    /* We found our special 'NaN' keyword! */
                    offset += nan_strlen;

                    inf_or_nan = 1;
                }
            }

            if (inf_or_nan) {
                if (offset < size) {
                    switch (src[offset]) {
                        default:
                            break;
                        case '0':
                        case '1':
                        case '2':
                        case '3':
                        case '4':
                        case '5':
                        case '6':
                        case '7':
                        case '8':
                        case '9':
                        case 'e':
                        case 'E':
                            /* cannot follow an inf or nan with digits! */
                            state->error = FlJsonParseError_InvalidNumberFormat;
                            state->offset = offset;
                            return 1;
                    }
                }
            }
        }

        if (found_sign && !inf_or_nan && (offset < size) && !('0' <= src[offset] && src[offset] <= '9')) {
            /* check if we are allowing leading '.'. */
            if (!(FlJsonParseFlags_AllowLeadingOrTrailingDecimalPoint & flags_bitset) || ('.' != src[offset])) {
                /* a leading '-' must be immediately followed by any digit! */
                state->error = FlJsonParseError_InvalidNumberFormat;
                state->offset = offset;
                return 1;
            }
        }

        if ((offset < size) && ('0' == src[offset])) {
            /* skip valid '0'. */
            offset++;

            /* we need to record whether we had any leading digits for checks later.
             */
            had_leading_digits = 1;

            if ((offset < size) && ('0' <= src[offset] && src[offset] <= '9')) {
                /* a leading '0' must not be immediately followed by any digit! */
                state->error = FlJsonParseError_InvalidNumberFormat;
                state->offset = offset;
                return 1;
            }
        }

        /* the main digits of our number next. */
        while ((offset < size) && ('0' <= src[offset] && src[offset] <= '9')) {
            offset++;

            /* we need to record whether we had any leading digits for checks later.
             */
            had_leading_digits = 1;
        }

        if ((offset < size) && ('.' == src[offset])) {
            offset++;

            if ((offset >= size) || !('0' <= src[offset] && src[offset] <= '9')) {
                if (!(FlJsonParseFlags_AllowLeadingOrTrailingDecimalPoint & flags_bitset) || !had_leading_digits) {
                    /* a decimal point must be followed by at least one digit. */
                    state->error = FlJsonParseError_InvalidNumberFormat;
                    state->offset = offset;
                    return 1;
                }
            }

            /* a decimal point can be followed by more digits of course! */
            while ((offset < size) && ('0' <= src[offset] && src[offset] <= '9')) {
                offset++;
            }
        }

        if ((offset < size) && ('e' == src[offset] || 'E' == src[offset])) {
            /* our number has an exponent! Skip 'e' or 'E'. */
            offset++;

            if ((offset < size) && ('-' == src[offset] || '+' == src[offset])) {
                /* skip optional '-' or '+'. */
                offset++;
            }

            if ((offset < size) && !('0' <= src[offset] && src[offset] <= '9')) {
                /* an exponent must have at least one digit! */
                state->error = FlJsonParseError_InvalidNumberFormat;
                state->offset = offset;
                return 1;
            }

            /* consume exponent digits. */
            do {
                offset++;
            } while ((offset < size) && ('0' <= src[offset] && src[offset] <= '9'));
        }
    }

    if (offset < size) {
        switch (src[offset]) {
            case ' ':
            case '\t':
            case '\r':
            case '\n':
            case '}':
            case ',':
            case ']':
                /* all of the above are ok. */
                break;
            case '=':
                if (FlJsonParseFlags_AllowEqualsInObject & flags_bitset) {
                    break;
                }

                state->error = FlJsonParseError_InvalidNumberFormat;
                state->offset = offset;
                return 1;
            default:
                state->error = FlJsonParseError_InvalidNumberFormat;
                state->offset = offset;
                return 1;
        }
    }

    state->data_size += offset - state->offset;

    /* one more byte for null terminator ending the number string! */
    state->data_size++;

    /* update offset. */
    state->offset = offset;

    return 0;
}

json_weak int json_get_value_size(struct json_parse_state_s* state, int is_global_object);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_get_value_size(struct json_parse_state_s* state, int is_global_object) {
    const size_t flags_bitset = state->flags_bitset;
    const char* const src = state->src;
    size_t offset;
    const size_t size = state->size;

    state->dom_size += sizeof(struct JsonValueEx);

    if (is_global_object) {
        int error;
        if (json_depth_push(state)) {
            return 1;
        }
        error = json_get_object_size(state, /* is_global_object = */ 1);
        state->depth--;
        return error;
    } else {
        if (json_skip_all_skippables(state)) {
            state->error = FlJsonParseError_PrematureEndOfBuffer;
            return 1;
        }

        /* can cache offset now. */
        offset = state->offset;

        switch (src[offset]) {
            case '"':
                return json_get_string_size(state, 0);
            case '\'':
                if (FlJsonParseFlags_AllowSingleQuotedStrings & flags_bitset) {
                    return json_get_string_size(state, 0);
                } else {
                    /* invalid value! */
                    state->error = FlJsonParseError_InvalidValue;
                    return 1;
                }
            case '{': {
                int error;
                if (json_depth_push(state)) {
                    return 1;
                }
                error = json_get_object_size(state, /* is_global_object = */ 0);
                state->depth--;
                return error;
            }
            case '[': {
                int error;
                if (json_depth_push(state)) {
                    return 1;
                }
                error = json_get_array_size(state);
                state->depth--;
                return error;
            }
            case '-':
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                return json_get_number_size(state);
            case '+':
                if (FlJsonParseFlags_AllowLeadingPlusSign & flags_bitset) {
                    return json_get_number_size(state);
                } else {
                    /* invalid value! */
                    state->error = FlJsonParseError_InvalidNumberFormat;
                    return 1;
                }
            case '.':
                if (FlJsonParseFlags_AllowLeadingOrTrailingDecimalPoint & flags_bitset) {
                    return json_get_number_size(state);
                } else {
                    /* invalid value! */
                    state->error = FlJsonParseError_InvalidNumberFormat;
                    return 1;
                }
            default:
                if ((offset + 4) <= size && 't' == src[offset + 0] && 'r' == src[offset + 1] && 'u' == src[offset + 2]
                    && 'e' == src[offset + 3]) {
                    state->offset += 4;
                    return 0;
                } else if ((offset + 5) <= size && 'f' == src[offset + 0] && 'a' == src[offset + 1]
                           && 'l' == src[offset + 2] && 's' == src[offset + 3] && 'e' == src[offset + 4]) {
                    state->offset += 5;
                    return 0;
                } else if ((offset + 4) <= size && 'n' == state->src[offset + 0] && 'u' == state->src[offset + 1]
                           && 'l' == state->src[offset + 2] && 'l' == state->src[offset + 3]) {
                    state->offset += 4;
                    return 0;
                } else if ((FlJsonParseFlags_AllowInfAndNan & flags_bitset) && (offset + 3) <= size
                           && 'N' == src[offset + 0] && 'a' == src[offset + 1] && 'N' == src[offset + 2]) {
                    return json_get_number_size(state);
                } else if ((FlJsonParseFlags_AllowInfAndNan & flags_bitset) && (offset + 8) <= size
                           && 'I' == src[offset + 0] && 'n' == src[offset + 1] && 'f' == src[offset + 2]
                           && 'i' == src[offset + 3] && 'n' == src[offset + 4] && 'i' == src[offset + 5]
                           && 't' == src[offset + 6] && 'y' == src[offset + 7]) {
                    return json_get_number_size(state);
                }

                /* invalid value! */
                state->error = FlJsonParseError_InvalidValue;
                return 1;
        }
    }
}

json_weak void json_parse_value(struct json_parse_state_s* state, int is_global_object, struct FlJsonValue* value);

json_weak void json_parse_string(struct json_parse_state_s* state, FlString* string);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_parse_string(struct json_parse_state_s* state, FlString* string) {
    size_t offset = state->offset;
    size_t bytes_written = 0;
    const char* const src = state->src;
    const char quote_to_use = '\'' == src[offset] ? '\'' : '"';
    char* data = state->data;
    unsigned long high_surrogate = 0;
    unsigned long codepoint;

    string->data = data;
    string->is_static = 0;

    /* skip leading '"' or '\''. */
    offset++;

    while (quote_to_use != src[offset]) {
        if ('\\' == src[offset]) {
            /* skip the reverse solidus. */
            offset++;

            switch (src[offset++]) {
                default:
                    return; /* we cannot ever reach here. */
                case 'u': {
                    codepoint = 0;
                    if (!json_hexadecimal_value(&src[offset], 4, &codepoint)) {
                        return; /* this shouldn't happen as the value was already validated.
                                 */
                    }

                    offset += 4;

                    if (codepoint <= 0x7fu) {
                        data[bytes_written++] = (char)codepoint; /* 0xxxxxxx. */
                    } else if (codepoint <= 0x7ffu) {
                        data[bytes_written++] = (char)(0xc0u | (codepoint >> 6));    /* 110xxxxx. */
                        data[bytes_written++] = (char)(0x80u | (codepoint & 0x3fu)); /* 10xxxxxx. */
                    } else if (codepoint >= 0xd800 && codepoint <= 0xdbff) {         /* high surrogate. */
                        high_surrogate = codepoint;
                        continue; /* we need the low half to form a complete codepoint. */
                    } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) { /* low surrogate. */
                        /* combine with the previously read half to obtain the complete
                         * codepoint. */
                        const unsigned long surrogate_offset = 0x10000u - (0xD800u << 10) - 0xDC00u;
                        codepoint = (high_surrogate << 10) + codepoint + surrogate_offset;
                        high_surrogate = 0;
                        data[bytes_written++] = (char)(0xF0u | (codepoint >> 18));           /* 11110xxx. */
                        data[bytes_written++] = (char)(0x80u | ((codepoint >> 12) & 0x3fu)); /* 10xxxxxx. */
                        data[bytes_written++] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));  /* 10xxxxxx. */
                        data[bytes_written++] = (char)(0x80u | (codepoint & 0x3fu));         /* 10xxxxxx. */
                    } else {
                        /* we assume the value was validated and thus is within the valid
                         * range. */
                        data[bytes_written++] = (char)(0xe0u | (codepoint >> 12));          /* 1110xxxx. */
                        data[bytes_written++] = (char)(0x80u | ((codepoint >> 6) & 0x3fu)); /* 10xxxxxx. */
                        data[bytes_written++] = (char)(0x80u | (codepoint & 0x3fu));        /* 10xxxxxx. */
                    }
                } break;
                case '"':
                    data[bytes_written++] = '"';
                    break;
                case '\\':
                    data[bytes_written++] = '\\';
                    break;
                case '/':
                    data[bytes_written++] = '/';
                    break;
                case 'b':
                    data[bytes_written++] = '\b';
                    break;
                case 'f':
                    data[bytes_written++] = '\f';
                    break;
                case 'n':
                    data[bytes_written++] = '\n';
                    break;
                case 'r':
                    data[bytes_written++] = '\r';
                    break;
                case 't':
                    data[bytes_written++] = '\t';
                    break;
                case '\r':
                    data[bytes_written++] = '\r';

                    /* check if we have a "\r\n" sequence. */
                    if ('\n' == src[offset]) {
                        data[bytes_written++] = '\n';
                        offset++;
                    }

                    break;
                case '\n':
                    data[bytes_written++] = '\n';
                    break;
            }
        } else {
            /* copy the character. */
            data[bytes_written++] = src[offset++];
        }
    }

    /* skip trailing '"' or '\''. */
    offset++;

    /* record the size of the string. */
    string->length = bytes_written;

    /* add null terminator to string. */
    data[bytes_written++] = '\0';

    /* move data along. */
    state->data += bytes_written;

    /* update offset. */
    state->offset = offset;
}

json_weak void json_parse_key(struct json_parse_state_s* state, FlString* string);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_parse_key(struct json_parse_state_s* state, FlString* string) {
    if (FlJsonParseFlags_AllowUnquotedKeys & state->flags_bitset) {
        const char* const src = state->src;
        char* const data = state->data;
        size_t offset = state->offset;

        /* if we are allowing unquoted keys, check for quoted anyway... */
        if (('"' == src[offset]) || ('\'' == src[offset])) {
            /* ... if we got a quote, just parse the key as a string as normal. */
            json_parse_string(state, string);
        } else {
            size_t size = 0;

            string->data = state->data;
            string->is_static = 0;

            while (is_valid_unquoted_key_char(src[offset])) {
                data[size++] = src[offset++];
            }

            /* add null terminator to string. */
            data[size] = '\0';

            /* record the size of the string. */
            string->length = size++;

            /* move data along. */
            state->data += size;

            /* update offset. */
            state->offset = offset;
        }
    } else {
        /* we are only allowed to have quoted keys, so just parse a string! */
        json_parse_string(state, string);
    }
}

json_weak void json_parse_object(struct json_parse_state_s* state, int is_global_object, struct FlJsonObject* object);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_parse_object(struct json_parse_state_s* state, int is_global_object, struct FlJsonObject* object) {
    const size_t flags_bitset = state->flags_bitset;
    const size_t size = state->size;
    const char* const src = state->src;
    size_t elements = 0;
    int allow_comma = 0;
    struct FlJsonObjectElement* previous = json_null;

    if (is_global_object) {
        /* if we skipped some whitespace, and then found an opening '{' of an. */
        /* object, we actually have a normal JSON object at the root of the DOM...
         */
        if ('{' == src[state->offset]) {
            /* . and we don't actually have a global object after all! */
            is_global_object = 0;
        }
    }

    if (!is_global_object) {
        /* skip leading '{'. */
        state->offset++;
    }

    (void)json_skip_all_skippables(state);

    /* reset elements. */
    elements = 0;

    while (state->offset < size) {
        struct FlJsonObjectElement* element = json_null;
        FlString* string = json_null;
        struct FlJsonValue* value = json_null;

        if (!is_global_object) {
            (void)json_skip_all_skippables(state);

            if ('}' == src[state->offset]) {
                /* skip trailing '}'. */
                state->offset++;

                /* finished the object! */
                break;
            }
        } else {
            if (json_skip_all_skippables(state)) {
                /* global object ends when the file ends! */
                break;
            }
        }

        /* if we parsed at least one element previously, grok for a comma. */
        if (allow_comma) {
            if (',' == src[state->offset]) {
                /* skip comma. */
                state->offset++;
                allow_comma = 0;
                continue;
            }
        }

        element = (struct FlJsonObjectElement*)state->dom;

        state->dom += sizeof(struct FlJsonObjectElement);

        if (json_null == previous) {
            /* this is our first element, so record it in our object. */
            object->start = element;
        } else {
            previous->next = element;
        }

        previous = element;

        if (FlJsonParseFlags_AllowLocationInformation & flags_bitset) {
            struct JsonStringEx* string_ex = (struct JsonStringEx*)state->dom;
            state->dom += sizeof(struct JsonStringEx);

            string_ex->offset = state->offset;
            string_ex->line_no = state->line_no;
            string_ex->row_no = state->offset - state->line_offset;

            string = &(string_ex->string);
        } else {
            string = (FlString*)state->dom;
            state->dom += sizeof(FlString);
        }

        element->name = string;

        (void)json_parse_key(state, string);

        (void)json_skip_all_skippables(state);

        /* skip colon or equals. */
        state->offset++;

        (void)json_skip_all_skippables(state);

        value = json_alloc_dom_value(state);

        element->value = value;

        json_parse_value(state, /* is_global_object = */ 0, value);

        /* successfully parsed a name/value pair! */
        elements++;
        allow_comma = 1;
    }

    /* if we had at least one element, end the linked list. */
    if (previous) {
        previous->next = json_null;
    }

    if (0 == elements) {
        object->start = json_null;
    }

    object->length = elements;
}

json_weak void json_parse_fixed_array(struct json_parse_state_s* state, struct FlJsonArray* array);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_parse_fixed_array(struct json_parse_state_s* state, struct FlJsonArray* array) {
    const char* const src = state->src;
    const size_t size = state->size;
    size_t elements = 0;
    int allow_comma = 0;
    struct FlJsonArrayElement* previous = json_null;

    /* skip leading '['. */
    state->offset++;

    (void)json_skip_all_skippables(state);

    /* reset elements. */
    elements = 0;

    do {
        struct FlJsonArrayElement* element = json_null;
        struct FlJsonValue* value = json_null;

        (void)json_skip_all_skippables(state);

        if (']' == src[state->offset]) {
            /* skip trailing ']'. */
            state->offset++;

            /* finished the array! */
            break;
        }

        /* if we parsed at least one element previously, grok for a comma. */
        if (allow_comma) {
            if (',' == src[state->offset]) {
                /* skip comma. */
                state->offset++;
                allow_comma = 0;
                continue;
            }
        }

        element = (struct FlJsonArrayElement*)state->dom;

        state->dom += sizeof(struct FlJsonArrayElement);

        if (json_null == previous) {
            /* this is our first element, so record it in our array. */
            array->start = element;
        } else {
            previous->next = element;
        }

        previous = element;

        value = json_alloc_dom_value(state);

        element->value = value;

        json_parse_value(state, /* is_global_object = */ 0, value);

        /* successfully parsed an array element! */
        elements++;
        allow_comma = 1;
    } while (state->offset < size);

    /* end the linked list. */
    if (previous) {
        previous->next = json_null;
    }

    if (0 == elements) {
        array->start = json_null;
    }

    array->length = elements;
}

json_weak void json_parse_number(struct json_parse_state_s* state, struct FlJsonNumber* number);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_parse_number(struct json_parse_state_s* state, struct FlJsonNumber* number) {
    const size_t flags_bitset = state->flags_bitset;
    size_t offset = state->offset;
    const size_t size = state->size;
    size_t bytes_written = 0;
    const char* const src = state->src;
    char* data = state->data;

    number->number = data;

    if (FlJsonParseFlags_AllowHexadecimalNumbers & flags_bitset) {
        if (('0' == src[offset]) && (('x' == src[offset + 1]) || ('X' == src[offset + 1]))) {
            /* consume hexadecimal digits. */
            while ((offset < size)
                   && (('0' <= src[offset] && src[offset] <= '9') || ('a' <= src[offset] && src[offset] <= 'f')
                       || ('A' <= src[offset] && src[offset] <= 'F') || ('x' == src[offset]) || ('X' == src[offset]))) {
                data[bytes_written++] = src[offset++];
            }
        }
    }

    while (offset < size) {
        int end = 0;

        switch (src[offset]) {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '.':
            case 'e':
            case 'E':
            case '+':
            case '-':
                data[bytes_written++] = src[offset++];
                break;
            default:
                end = 1;
                break;
        }

        if (0 != end) {
            break;
        }
    }

    if (FlJsonParseFlags_AllowInfAndNan & flags_bitset) {
        const size_t inf_strlen = 8; /* = strlen("Infinity");. */
        const size_t nan_strlen = 3; /* = strlen("NaN");. */

        if (offset + inf_strlen < size) {
            if ('I' == src[offset]) {
                size_t i;
                /* We found our special 'Infinity' keyword! */
                for (i = 0; i < inf_strlen; i++) {
                    data[bytes_written++] = src[offset++];
                }
            }
        }

        if (offset + nan_strlen < size) {
            if ('N' == src[offset]) {
                size_t i;
                /* We found our special 'NaN' keyword! */
                for (i = 0; i < nan_strlen; i++) {
                    data[bytes_written++] = src[offset++];
                }
            }
        }
    }

    /* record the size of the number. */
    number->number_size = bytes_written;
    /* add null terminator to number string. */
    data[bytes_written++] = '\0';
    /* move data along. */
    state->data += bytes_written;
    /* update offset. */
    state->offset = offset;
}

json_weak void json_parse_value(struct json_parse_state_s* state, int is_global_object, struct FlJsonValue* value);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_parse_value(struct json_parse_state_s* state, int is_global_object, struct FlJsonValue* value) {
    const size_t flags_bitset = state->flags_bitset;
    const char* const src = state->src;
    const size_t size = state->size;
    size_t offset;

    (void)json_skip_all_skippables(state);

    /* cache offset now. */
    offset = state->offset;

    if (is_global_object) {
        value->type = FlJsonType_Object;
        value->payload = state->dom;
        state->dom += sizeof(struct FlJsonObject);
        if (json_depth_push(state)) {
            /* unreachable after a depth-gated sizing pass; stop descending and
             * jam to end-of-input so every enclosing parse loop terminates. */
            ((struct FlJsonObject*)value->payload)->start = json_null;
            ((struct FlJsonObject*)value->payload)->length = 0;
            state->offset = state->size;
            return;
        }
        json_parse_object(state, /* is_global_object = */ 1, (struct FlJsonObject*)value->payload);
        state->depth--;
    } else {
        switch (src[offset]) {
            case '"':
            case '\'':
                value->type = FlJsonType_String;
                value->payload = state->dom;
                state->dom += sizeof(FlString);
                json_parse_string(state, (FlString*)value->payload);
                break;
            case '{':
                value->type = FlJsonType_Object;
                value->payload = state->dom;
                state->dom += sizeof(struct FlJsonObject);
                if (json_depth_push(state)) {
                    /* unreachable after a depth-gated sizing pass; stop descending and
                     * jam to end-of-input so every enclosing parse loop terminates. */
                    ((struct FlJsonObject*)value->payload)->start = json_null;
                    ((struct FlJsonObject*)value->payload)->length = 0;
                    state->offset = state->size;
                    break;
                }
                json_parse_object(state, /* is_global_object = */ 0, (struct FlJsonObject*)value->payload);
                state->depth--;
                break;
            case '[':
                value->type = FlJsonType_Array;
                value->payload = state->dom;
                state->dom += sizeof(struct FlJsonArray);
                if (json_depth_push(state)) {
                    /* unreachable after a depth-gated sizing pass; stop descending and
                     * jam to end-of-input so every enclosing parse loop terminates. */
                    ((struct FlJsonArray*)value->payload)->start = json_null;
                    ((struct FlJsonArray*)value->payload)->length = 0;
                    state->offset = state->size;
                    break;
                }
                json_parse_fixed_array(state, (struct FlJsonArray*)value->payload);
                state->depth--;
                break;
            case '-':
            case '+':
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '.':
                value->type = FlJsonType_Number;
                value->payload = state->dom;
                state->dom += sizeof(struct FlJsonNumber);
                json_parse_number(state, (struct FlJsonNumber*)value->payload);
                break;
            default:
                if ((offset + 4) <= size && 't' == src[offset + 0] && 'r' == src[offset + 1] && 'u' == src[offset + 2]
                    && 'e' == src[offset + 3]) {
                    value->type = FlJsonType_True;
                    value->payload = json_null;
                    state->offset += 4;
                } else if ((offset + 5) <= size && 'f' == src[offset + 0] && 'a' == src[offset + 1]
                           && 'l' == src[offset + 2] && 's' == src[offset + 3] && 'e' == src[offset + 4]) {
                    value->type = FlJsonType_False;
                    value->payload = json_null;
                    state->offset += 5;
                } else if ((offset + 4) <= size && 'n' == src[offset + 0] && 'u' == src[offset + 1]
                           && 'l' == src[offset + 2] && 'l' == src[offset + 3]) {
                    value->type = FlJsonType_Null;
                    value->payload = json_null;
                    state->offset += 4;
                } else if ((FlJsonParseFlags_AllowInfAndNan & flags_bitset) && (offset + 3) <= size
                           && 'N' == src[offset + 0] && 'a' == src[offset + 1] && 'N' == src[offset + 2]) {
                    value->type = FlJsonType_Number;
                    value->payload = state->dom;
                    state->dom += sizeof(struct FlJsonNumber);
                    json_parse_number(state, (struct FlJsonNumber*)value->payload);
                } else if ((FlJsonParseFlags_AllowInfAndNan & flags_bitset) && (offset + 8) <= size
                           && 'I' == src[offset + 0] && 'n' == src[offset + 1] && 'f' == src[offset + 2]
                           && 'i' == src[offset + 3] && 'n' == src[offset + 4] && 'i' == src[offset + 5]
                           && 't' == src[offset + 6] && 'y' == src[offset + 7]) {
                    value->type = FlJsonType_Number;
                    value->payload = state->dom;
                    state->dom += sizeof(struct FlJsonNumber);
                    json_parse_number(state, (struct FlJsonNumber*)value->payload);
                }
                break;
        }
    }
}

struct FlJsonValue* fl_json_parse_with_flags(FlArena* arena, const void* src, size_t src_size, size_t flags_bitset,
                                             struct FlJsonParseResult* result) {
    struct json_parse_state_s state;
    void* allocation;
    struct FlJsonValue* value;
    size_t total_size;
    int input_error;

    if (result) {
        result->error = FlJsonParseError_None;
        result->error_offset = 0;
        result->error_line_no = 0;
        result->error_row_no = 0;
    }

    if (json_null == src) {
        /* invalid src pointer was null! */
        return json_null;
    }

    state.src = (const char*)src;
    state.size = src_size;
    state.offset = 0;
    state.line_no = 1;
    state.line_offset = 0;
    state.depth = 0;
    state.error = FlJsonParseError_None;
    state.dom_size = 0;
    state.data_size = 0;
    state.flags_bitset = flags_bitset;

    input_error = json_get_value_size(&state, (int)(FlJsonParseFlags_AllowGlobalObject & state.flags_bitset));

    if (0 == input_error) {
        json_skip_all_skippables(&state);

        if (state.offset != state.size) {
            /* our parsing didn't have an error, but there are characters remaining in
             * the input that weren't part of the JSON! */

            state.error = FlJsonParseError_UnexpectedTrailingCharacters;
            input_error = 1;
        }
    }

    if (input_error) {
        /* parsing value's size failed (most likely an invalid JSON DOM!). */
        if (result) {
            result->error = state.error;
            result->error_offset = state.offset;
            result->error_line_no = state.line_no;
            result->error_row_no = state.offset - state.line_offset;
        }
        return json_null;
    }

    /* our total allocation is the combination of the dom and data sizes (we. */
    /* first encode the structure of the JSON, and then the data referenced by. */
    /* the JSON values). */
    total_size = state.dom_size + state.data_size;

    allocation = arena_alloc_array(arena, u8, total_size);

    /* reset offset so we can reuse it. */
    state.offset = 0;

    /* reset the line information so we can reuse it. */
    state.line_no = 1;
    state.line_offset = 0;
    state.depth = 0;

    state.dom = (char*)allocation;
    state.data = state.dom + state.dom_size;

    value = json_alloc_dom_value(&state);

    json_parse_value(&state, (int)(FlJsonParseFlags_AllowGlobalObject & state.flags_bitset), value);

    return (struct FlJsonValue*)allocation;
}

#define JSON_PARSE_DEFAULT_FLAGS (FlJsonParseFlags_AllowJson5 & ~FlJsonParseFlags_AllowGlobalObject)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlJsonValue* fl_json_parse(FlArena* arena, const void* src, size_t src_size) {
    return fl_json_parse_with_flags(arena, src, src_size,
                                    JSON_PARSE_DEFAULT_FLAGS | FlJsonParseFlags_AllowLocationInformation, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlJsonValue* fl_json_parse_no_location(FlArena* arena, const void* src, size_t src_size) {
    return fl_json_parse_with_flags(arena, src, src_size, JSON_PARSE_DEFAULT_FLAGS, nullptr);
}

struct json_extract_result_s {
    size_t dom_size;
    size_t data_size;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* json_arena_alloc_wrapper(void* user_data, size_t size) {
    FlArena* arena = (FlArena*)user_data;
    return arena_alloc_array(arena, u8, size);
}

struct FlJsonValue* json_extract_value_ex(const struct FlJsonValue* value, void* (*alloc_func_ptr)(void*, size_t),
                                          void* user_data);

struct FlJsonValue* fl_json_extract_value(FlArena* arena, const struct FlJsonValue* value) {
    return json_extract_value_ex(value, json_arena_alloc_wrapper, arena);
}

json_weak struct json_extract_result_s json_extract_get_number_size(const struct FlJsonNumber* const number);
json_weak struct json_extract_result_s json_extract_get_string_size(const FlString* const string);
json_weak struct json_extract_result_s json_extract_get_object_size(const struct FlJsonObject* const object);
json_weak struct json_extract_result_s json_extract_get_array_size(const struct FlJsonArray* const array);
json_weak struct json_extract_result_s json_extract_get_value_size(const struct FlJsonValue* const value);

struct json_extract_result_s json_extract_get_number_size(const struct FlJsonNumber* const number) {
    struct json_extract_result_s result;
    result.dom_size = sizeof(struct FlJsonNumber);
    result.data_size = number->number_size + 1;
    return result;
}

struct json_extract_result_s json_extract_get_string_size(const FlString* const string) {
    struct json_extract_result_s result;
    result.dom_size = sizeof(FlString);
    result.data_size = string->length + 1;
    return result;
}

struct json_extract_result_s json_extract_get_object_size(const struct FlJsonObject* const object) {
    struct json_extract_result_s result;
    size_t i;
    const struct FlJsonObjectElement* element = object->start;

    result.dom_size = sizeof(struct FlJsonObject) + (sizeof(struct FlJsonObjectElement) * object->length);
    result.data_size = 0;

    for (i = 0; i < object->length; i++) {
        const struct json_extract_result_s string_result = json_extract_get_string_size(element->name);
        const struct json_extract_result_s value_result = json_extract_get_value_size(element->value);

        result.dom_size += string_result.dom_size;
        result.data_size += string_result.data_size;

        result.dom_size += value_result.dom_size;
        result.data_size += value_result.data_size;

        element = element->next;
    }

    return result;
}

struct json_extract_result_s json_extract_get_array_size(const struct FlJsonArray* const array) {
    struct json_extract_result_s result;
    size_t i;
    const struct FlJsonArrayElement* element = array->start;

    result.dom_size = sizeof(struct FlJsonArray) + (sizeof(struct FlJsonArrayElement) * array->length);
    result.data_size = 0;

    for (i = 0; i < array->length; i++) {
        const struct json_extract_result_s value_result = json_extract_get_value_size(element->value);

        result.dom_size += value_result.dom_size;
        result.data_size += value_result.data_size;

        element = element->next;
    }

    return result;
}

struct json_extract_result_s json_extract_get_value_size(const struct FlJsonValue* const value) {
    struct json_extract_result_s result = { 0, 0 };

    switch (value->type) {
        default:
            break;
        case FlJsonType_Object:
            result = json_extract_get_object_size((const struct FlJsonObject*)value->payload);
            break;
        case FlJsonType_Array:
            result = json_extract_get_array_size((const struct FlJsonArray*)value->payload);
            break;
        case FlJsonType_Number:
            result = json_extract_get_number_size((const struct FlJsonNumber*)value->payload);
            break;
        case FlJsonType_String:
            result = json_extract_get_string_size((const FlString*)value->payload);
            break;
    }

    result.dom_size += sizeof(struct JsonValueEx);

    return result;
}

struct json_extract_state_s {
    char* dom;
    char* data;
};

json_weak void json_extract_copy_value(struct json_extract_state_s* const state, const struct FlJsonValue* const value);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_extract_copy_value(struct json_extract_state_s* const state, const struct FlJsonValue* const value) {
    FlString* string;
    struct FlJsonNumber* number;
    struct FlJsonObject* object;
    struct FlJsonArray* array;
    struct FlJsonValue* new_value;

    // The extracted copy keeps no location fields.
    struct JsonValueEx* new_value_ex = (struct JsonValueEx*)state->dom;
    *new_value_ex = (struct JsonValueEx) { 0 };
    memory_copy(&new_value_ex->value, sizeof(struct FlJsonValue), value, sizeof(struct FlJsonValue));

    new_value = &new_value_ex->value;
    state->dom += sizeof(struct JsonValueEx);
    new_value->payload = state->dom;

    if (FlJsonType_String == value->type) {
        memory_copy(state->dom, sizeof(FlString), value->payload, sizeof(FlString));
        string = (FlString*)state->dom;
        state->dom += sizeof(FlString);

        memory_copy(state->data, string->length + 1, string->data, string->length + 1);
        string->data = state->data;
        state->data += string->length + 1;
    } else if (FlJsonType_Number == value->type) {
        memory_copy(state->dom, sizeof(struct FlJsonNumber), value->payload, sizeof(struct FlJsonNumber));
        number = (struct FlJsonNumber*)state->dom;
        state->dom += sizeof(struct FlJsonNumber);

        memory_copy(state->data, number->number_size, number->number, number->number_size);
        state->data[number->number_size] = '\0';
        number->number = state->data;
        state->data += number->number_size + 1;
    } else if (FlJsonType_Object == value->type) {
        struct FlJsonObjectElement* element;
        size_t i;

        memory_copy(state->dom, sizeof(struct FlJsonObject), value->payload, sizeof(struct FlJsonObject));
        object = (struct FlJsonObject*)state->dom;
        state->dom += sizeof(struct FlJsonObject);

        element = object->start;
        object->start = (struct FlJsonObjectElement*)state->dom;

        for (i = 0; i < object->length; i++) {
            struct FlJsonValue* previous_value;
            struct FlJsonObjectElement* previous_element;

            memory_copy(state->dom, sizeof(struct FlJsonObjectElement), element, sizeof(struct FlJsonObjectElement));
            element = (struct FlJsonObjectElement*)state->dom;
            state->dom += sizeof(struct FlJsonObjectElement);

            string = element->name;
            memory_copy(state->dom, sizeof(FlString), string, sizeof(FlString));
            string = (FlString*)state->dom;
            state->dom += sizeof(FlString);
            element->name = string;

            memory_copy(state->data, string->length + 1, string->data, string->length + 1);
            string->data = state->data;
            state->data += string->length + 1;

            previous_value = element->value;
            element->value = (struct FlJsonValue*)state->dom;
            json_extract_copy_value(state, previous_value);

            previous_element = element;
            element = element->next;

            if (element) {
                previous_element->next = (struct FlJsonObjectElement*)state->dom;
            }
        }
    } else if (FlJsonType_Array == value->type) {
        struct FlJsonArrayElement* element;
        size_t i;

        memory_copy(state->dom, sizeof(struct FlJsonArray), value->payload, sizeof(struct FlJsonArray));
        array = (struct FlJsonArray*)state->dom;
        state->dom += sizeof(struct FlJsonArray);

        element = array->start;
        array->start = (struct FlJsonArrayElement*)state->dom;

        for (i = 0; i < array->length; i++) {
            struct FlJsonValue* previous_value;
            struct FlJsonArrayElement* previous_element;

            memory_copy(state->dom, sizeof(struct FlJsonArrayElement), element, sizeof(struct FlJsonArrayElement));
            element = (struct FlJsonArrayElement*)state->dom;
            state->dom += sizeof(struct FlJsonArrayElement);

            previous_value = element->value;
            element->value = (struct FlJsonValue*)state->dom;
            json_extract_copy_value(state, previous_value);

            previous_element = element;
            element = element->next;

            if (element) {
                previous_element->next = (struct FlJsonArrayElement*)state->dom;
            }
        }
    }
}

struct FlJsonValue* json_extract_value_ex(const struct FlJsonValue* value, void* (*alloc_func_ptr)(void*, size_t),
                                          void* user_data) {
    void* allocation;
    struct json_extract_result_s result;
    struct json_extract_state_s state;
    size_t total_size;

    if (json_null == value) {
        /* invalid value was null! */
        return json_null;
    }

    result = json_extract_get_value_size(value);
    total_size = result.dom_size + result.data_size;

    if (json_null == alloc_func_ptr) {
        return json_null;
    }

    allocation = alloc_func_ptr(user_data, total_size);

    state.dom = (char*)allocation;
    state.data = state.dom + result.dom_size;

    json_extract_copy_value(&state, value);

    return (struct FlJsonValue*)allocation;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_json_value_as_string(struct FlJsonValue* const value) {
    if (value->type != FlJsonType_String) {
        return (FlString) { 0 };
    }

    FlString* js = (FlString*)value->payload;
    if (!js || !js->data) {
        return (FlString) { 0 };
    }

    return *js;
}

struct FlJsonNumber* fl_json_value_as_number(struct FlJsonValue* const value) {
    if (value->type != FlJsonType_Number) {
        return json_null;
    }

    return (struct FlJsonNumber*)value->payload;
}

struct FlJsonObject* fl_json_value_as_object(struct FlJsonValue* const value) {
    if (value->type != FlJsonType_Object) {
        return json_null;
    }

    return (struct FlJsonObject*)value->payload;
}

struct FlJsonArray* fl_json_value_as_fixed_array(struct FlJsonValue* const value) {
    if (value->type != FlJsonType_Array) {
        return json_null;
    }

    return (struct FlJsonArray*)value->payload;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int fl_json_value_is_true(const struct FlJsonValue* const value) {
    return value->type == FlJsonType_True;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int fl_json_value_is_false(const struct FlJsonValue* const value) {
    return value->type == FlJsonType_False;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int fl_json_value_is_null(const struct FlJsonValue* const value) {
    return value->type == FlJsonType_Null;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int fl_json_value_is_bool(const FlJsonValue* value) {
    return value && (value->type == FlJsonType_True || value->type == FlJsonType_False);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int fl_json_value_get_bool(const FlJsonValue* value) {
    return value && value->type == FlJsonType_True;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Parses an unsigned decimal or 0x hexadecimal magnitude of any length; false on a bad digit or overflow.
static bool json_parse_magnitude(FlString text, u64* out) {
    u64 base = 10;
    if (text.length >= 2 && text.data[0] == '0' && (text.data[1] == 'x' || text.data[1] == 'X')) {
        base = 16;
        text = fl_string_substr(text, 2, text.length - 2);
    }
    if (text.length == 0) {
        return false;
    }

    u64 value = 0;
    for (u64 i = 0; i < text.length; i++) {
        char c = text.data[i];
        u64 digit;
        if (c >= '0' && c <= '9') {
            digit = (u64)(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = (u64)(c - 'a') + 10;
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = (u64)(c - 'A') + 10;
        } else {
            return false;
        }
        if (value > (UINT64_MAX - digit) / base) {
            return false;
        }
        value = value * base + digit;
    }

    *out = value;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_number_to_u64(const FlJsonNumber* num, u64* out) {
    FlString text = { .data = num->number, .length = num->number_size };
    if (text.length > 0 && text.data[0] == '+') {
        text = fl_string_substr(text, 1, text.length - 1);
    }
    return json_parse_magnitude(text, out);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_number_to_i64(const FlJsonNumber* num, i64* out) {
    FlString text = { .data = num->number, .length = num->number_size };
    bool negative = text.length > 0 && text.data[0] == '-';
    if (negative || (text.length > 0 && text.data[0] == '+')) {
        text = fl_string_substr(text, 1, text.length - 1);
    }

    u64 magnitude;
    if (!json_parse_magnitude(text, &magnitude)) {
        return false;
    }

    u64 limit = negative ? (u64)INT64_MAX + 1 : (u64)INT64_MAX;
    if (magnitude > limit) {
        return false;
    }

    *out = negative ? (i64)(0 - magnitude) : (i64)magnitude;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_number_to_f64(const FlJsonNumber* num, f64* out) {
    arena_scratch_auto(temp);
    char* text = arena_alloc_array(temp.arena, char, num->number_size + 1);
    memory_copy(text, num->number_size, num->number, num->number_size);
    text[num->number_size] = '\0';

    errno = 0;
    char* end;
    f64 value = strtod(text, &end);
    if (end == text || *end != '\0' || (errno == ERANGE && (value == HUGE_VAL || value == -HUGE_VAL))) {
        return false;
    }

    *out = value;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 fl_json_value_get_u64(const FlJsonValue* value) {
    u64 result = 0;
    const FlJsonNumber* num = value ? fl_json_value_as_number((FlJsonValue*)value) : nullptr;
    if (!num || !json_number_to_u64(num, &result)) {
        return 0;
    }
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i32 fl_json_value_get_i32(const FlJsonValue* value) {
    i64 result = 0;
    const FlJsonNumber* num = value ? fl_json_value_as_number((FlJsonValue*)value) : nullptr;
    if (!num || !json_number_to_i64(num, &result) || result < INT32_MIN || result > INT32_MAX) {
        return 0;
    }
    return (i32)result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

f64 fl_json_value_get_f64(const FlJsonValue* value) {
    f64 result = 0.0;
    const FlJsonNumber* num = value ? fl_json_value_as_number((FlJsonValue*)value) : nullptr;
    if (!num || !json_number_to_f64(num, &result)) {
        return 0.0;
    }
    return result;
}

json_weak int json_write_minified_get_value_size(const struct FlJsonValue* value, size_t* size);

json_weak int json_write_get_number_size(const struct FlJsonNumber* number, size_t* size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_write_get_number_size(const struct FlJsonNumber* number, size_t* size) {
    json_uintmax_t parsed_number;
    size_t i;

    if (number->number_size >= 2) {
        switch (number->number[1]) {
            default:
                break;
            case 'x':
            case 'X':
                /* the number is a FlJsonParseFlags_AllowHexadecimalNumbers hexadecimal
                 * so we have to do extra work to convert it to a non-hexadecimal for JSON
                 * output. */
                parsed_number = json_strtoumax(number->number, json_null, 0);

                i = 0;

                while (0 != parsed_number) {
                    parsed_number /= 10;
                    i++;
                }

                /* A zero value still needs one byte for the single '0' digit. */
                if (0 == i) {
                    i = 1;
                }

                *size += i;
                return 0;
        }
    }

    /* check to see if the number has leading/trailing decimal point. */
    i = 0;

    /* skip any leading '+' or '-'. */
    if ((i < number->number_size) && (('+' == number->number[i]) || ('-' == number->number[i]))) {
        i++;
    }

    /* check if we have infinity. */
    if ((i < number->number_size) && ('I' == number->number[i])) {
        const char* inf = "Infinity";
        size_t k;

        for (k = i; k < number->number_size; k++) {
            const char c = *inf;

            /* Check if we found the Infinity string! */
            if ('\0' == c) {
                break;
            } else if (c != number->number[k]) {
                break;
            }

            /* Advance only on a match. */
            inf++;
        }

        if ('\0' == *inf) {
            /* Inf becomes 1.7976931348623158e308 because JSON can't support it. */
            *size += 22;

            /* if we had a leading '-' we need to record it in the JSON output. */
            if ('-' == number->number[0]) {
                *size += 1;
            }
        }

        return 0;
    }

    /* check if we have nan. */
    if ((i < number->number_size) && ('N' == number->number[i])) {
        const char* nan = "NaN";
        size_t k;

        for (k = i; k < number->number_size; k++) {
            const char c = *nan;

            /* Check if we found the NaN string! */
            if ('\0' == c) {
                break;
            } else if (c != number->number[k]) {
                break;
            }

            /* Advance only on a match. */
            nan++;
        }

        if ('\0' == *nan) {
            /* NaN becomes 1 because JSON can't support it. */
            *size += 1;

            return 0;
        }
    }

    /* if we had a leading decimal point. */
    if ((i < number->number_size) && ('.' == number->number[i])) {
        /* 1 + because we had a leading decimal point. */
        *size += 1;
        goto cleanup;
    }

    for (; i < number->number_size; i++) {
        const char c = number->number[i];
        if (!('0' <= c && c <= '9')) {
            break;
        }
    }

    /* if we had a trailing decimal point. */
    if ((i + 1 == number->number_size) && ('.' == number->number[i])) {
        /* 1 + because we had a trailing decimal point. */
        *size += 1;
        goto cleanup;
    }

cleanup:
    *size += number->number_size; /* the actual string of the number. */

    /* if we had a leading '+' we don't record it in the JSON output. */
    if ('+' == number->number[0]) {
        *size -= 1;
    }

    return 0;
}

json_weak int json_write_get_string_size(const FlString* string, size_t* size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_write_get_string_size(const FlString* string, size_t* size) {
    size_t i;
    for (i = 0; i < string->length; i++) {
        switch (string->data[i]) {
            case '"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                *size += 2;
                break;
            default:
                *size += 1;
                break;
        }
    }

    *size += 2; /* need to encode the surrounding '"' characters. */

    return 0;
}

json_weak int json_write_minified_get_array_size(const struct FlJsonArray* array, size_t* size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_write_minified_get_array_size(const struct FlJsonArray* array, size_t* size) {
    struct FlJsonArrayElement* element;

    *size += 2; /* '[' and ']'. */

    if (1 < array->length) {
        *size += array->length - 1; /* ','s seperate each element. */
    }

    for (element = array->start; json_null != element; element = element->next) {
        if (json_write_minified_get_value_size(element->value, size)) {
            /* value was malformed! */
            return 1;
        }
    }

    return 0;
}

json_weak int json_write_minified_get_object_size(const struct FlJsonObject* object, size_t* size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_write_minified_get_object_size(const struct FlJsonObject* object, size_t* size) {
    struct FlJsonObjectElement* element;

    *size += 2; /* '{' and '}'. */

    *size += object->length; /* ':'s seperate each name/value pair. */

    if (1 < object->length) {
        *size += object->length - 1; /* ','s seperate each element. */
    }

    for (element = object->start; json_null != element; element = element->next) {
        if (json_write_get_string_size(element->name, size)) {
            /* string was malformed! */
            return 1;
        }

        if (json_write_minified_get_value_size(element->value, size)) {
            /* value was malformed! */
            return 1;
        }
    }

    return 0;
}

json_weak int json_write_minified_get_value_size(const struct FlJsonValue* value, size_t* size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_write_minified_get_value_size(const struct FlJsonValue* value, size_t* size) {
    switch (value->type) {
        default:
            /* unknown value type found! */
            return 1;
        case FlJsonType_Number:
            return json_write_get_number_size((struct FlJsonNumber*)value->payload, size);
        case FlJsonType_String:
            return json_write_get_string_size((FlString*)value->payload, size);
        case FlJsonType_Array:
            return json_write_minified_get_array_size((struct FlJsonArray*)value->payload, size);
        case FlJsonType_Object:
            return json_write_minified_get_object_size((struct FlJsonObject*)value->payload, size);
        case FlJsonType_True:
            *size += 4; /* the string "true". */
            return 0;
        case FlJsonType_False:
            *size += 5; /* the string "false". */
            return 0;
        case FlJsonType_Null:
            *size += 4; /* the string "null". */
            return 0;
    }
}

json_weak char* json_write_minified_value(const struct FlJsonValue* value, char* data);

json_weak char* json_write_number(const struct FlJsonNumber* number, char* data);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char* json_write_number(const struct FlJsonNumber* number, char* data) {
    json_uintmax_t parsed_number, backup;
    size_t i;

    if (number->number_size >= 2) {
        switch (number->number[1]) {
            default:
                break;
            case 'x':
            case 'X':
                /* The number is a FlJsonParseFlags_AllowHexadecimalNumbers hexadecimal
                 * so we have to do extra work to convert it to a non-hexadecimal for JSON
                 * output. */
                parsed_number = json_strtoumax(number->number, json_null, 0);

                /* We need a copy of parsed number twice, so take a backup of it. */
                backup = parsed_number;

                i = 0;

                while (0 != parsed_number) {
                    parsed_number /= 10;
                    i++;
                }

                /* A zero value has one digit ('0'). */
                if (0 == i) {
                    i = 1;
                }

                /* Restore parsed_number to its original value stored in the backup. */
                parsed_number = backup;

                /* Now use backup to take a copy of i, or the length of the string. */
                backup = i;

                do {
                    *(data + i - 1) = '0' + (char)(parsed_number % 10);
                    parsed_number /= 10;
                    i--;
                } while (0 != parsed_number);

                data += backup;

                return data;
        }
    }

    /* check to see if the number has leading/trailing decimal point. */
    i = 0;

    /* skip any leading '-'. */
    if ((i < number->number_size) && (('+' == number->number[i]) || ('-' == number->number[i]))) {
        i++;
    }

    /* check if we have infinity. */
    if ((i < number->number_size) && ('I' == number->number[i])) {
        const char* inf = "Infinity";
        size_t k;

        for (k = i; k < number->number_size; k++) {
            const char c = *inf;

            /* Check if we found the Infinity string! */
            if ('\0' == c) {
                break;
            } else if (c != number->number[k]) {
                break;
            }

            /* Advance only on a match. */
            inf++;
        }

        if ('\0' == *inf) {
            const char* dbl_max;

            /* if we had a leading '-' we need to record it in the JSON output. */
            if ('-' == number->number[0]) {
                *data++ = '-';
            }

            /* Inf becomes 1.7976931348623158e308 because JSON can't support it. */
            for (dbl_max = "1.7976931348623158e308"; '\0' != *dbl_max; dbl_max++) {
                *data++ = *dbl_max;
            }

            return data;
        }
    }

    /* check if we have nan. */
    if ((i < number->number_size) && ('N' == number->number[i])) {
        const char* nan = "NaN";
        size_t k;

        for (k = i; k < number->number_size; k++) {
            const char c = *nan;

            /* Check if we found the NaN string! */
            if ('\0' == c) {
                break;
            } else if (c != number->number[k]) {
                break;
            }

            /* Advance only on a match. */
            nan++;
        }

        if ('\0' == *nan) {
            /* NaN becomes 0 because JSON can't support it. */
            *data++ = '0';
            return data;
        }
    }

    /* if we had a leading decimal point. */
    if ((i < number->number_size) && ('.' == number->number[i])) {
        i = 0;

        /* skip any leading '+'. */
        if ('+' == number->number[i]) {
            i++;
        }

        /* output the leading '-' if we had one. */
        if ('-' == number->number[i]) {
            *data++ = '-';
            i++;
        }

        /* insert a '0' to fix the leading decimal point for JSON output. */
        *data++ = '0';

        /* and output the rest of the number as normal. */
        for (; i < number->number_size; i++) {
            *data++ = number->number[i];
        }

        return data;
    }

    for (; i < number->number_size; i++) {
        const char c = number->number[i];
        if (!('0' <= c && c <= '9')) {
            break;
        }
    }

    /* if we had a trailing decimal point. */
    if ((i + 1 == number->number_size) && ('.' == number->number[i])) {
        i = 0;

        /* skip any leading '+'. */
        if ('+' == number->number[i]) {
            i++;
        }

        /* output the leading '-' if we had one. */
        if ('-' == number->number[i]) {
            *data++ = '-';
            i++;
        }

        /* and output the rest of the number as normal. */
        for (; i < number->number_size; i++) {
            *data++ = number->number[i];
        }

        /* insert a '0' to fix the trailing decimal point for JSON output. */
        *data++ = '0';

        return data;
    }

    i = 0;

    /* skip any leading '+'. */
    if ('+' == number->number[i]) {
        i++;
    }

    for (; i < number->number_size; i++) {
        *data++ = number->number[i];
    }

    return data;
}

json_weak char* json_write_string(const FlString* string, char* data);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char* json_write_string(const FlString* string, char* data) {
    size_t i;

    *data++ = '"'; /* open the string. */

    for (i = 0; i < string->length; i++) {
        switch (string->data[i]) {
            case '"':
                *data++ = '\\'; /* escape the control character. */
                *data++ = '"';
                break;
            case '\\':
                *data++ = '\\'; /* escape the control character. */
                *data++ = '\\';
                break;
            case '\b':
                *data++ = '\\'; /* escape the control character. */
                *data++ = 'b';
                break;
            case '\f':
                *data++ = '\\'; /* escape the control character. */
                *data++ = 'f';
                break;
            case '\n':
                *data++ = '\\'; /* escape the control character. */
                *data++ = 'n';
                break;
            case '\r':
                *data++ = '\\'; /* escape the control character. */
                *data++ = 'r';
                break;
            case '\t':
                *data++ = '\\'; /* escape the control character. */
                *data++ = 't';
                break;
            default:
                *data++ = string->data[i];
                break;
        }
    }

    *data++ = '"'; /* close the string. */

    return data;
}

json_weak char* json_write_minified_fixed_array(const struct FlJsonArray* array, char* data);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char* json_write_minified_fixed_array(const struct FlJsonArray* array, char* data) {
    struct FlJsonArrayElement* element = json_null;

    *data++ = '['; /* open the array. */

    for (element = array->start; json_null != element; element = element->next) {
        if (element != array->start) {
            *data++ = ','; /* ','s seperate each element. */
        }

        data = json_write_minified_value(element->value, data);

        if (json_null == data) {
            /* value was malformed! */
            return json_null;
        }
    }

    *data++ = ']'; /* close the array. */

    return data;
}

json_weak char* json_write_minified_object(const struct FlJsonObject* object, char* data);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char* json_write_minified_object(const struct FlJsonObject* object, char* data) {
    struct FlJsonObjectElement* element = json_null;

    *data++ = '{'; /* open the object. */

    for (element = object->start; json_null != element; element = element->next) {
        if (element != object->start) {
            *data++ = ','; /* ','s seperate each element. */
        }

        data = json_write_string(element->name, data);

        if (json_null == data) {
            /* string was malformed! */
            return json_null;
        }

        *data++ = ':'; /* ':'s seperate each name/value pair. */

        data = json_write_minified_value(element->value, data);

        if (json_null == data) {
            /* value was malformed! */
            return json_null;
        }
    }

    *data++ = '}'; /* close the object. */

    return data;
}

json_weak char* json_write_minified_value(const struct FlJsonValue* value, char* data);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char* json_write_minified_value(const struct FlJsonValue* value, char* data) {
    switch (value->type) {
        default:
            /* unknown value type found! */
            return json_null;
        case FlJsonType_Number:
            return json_write_number((struct FlJsonNumber*)value->payload, data);
        case FlJsonType_String:
            return json_write_string((FlString*)value->payload, data);
        case FlJsonType_Array:
            return json_write_minified_fixed_array((struct FlJsonArray*)value->payload, data);
        case FlJsonType_Object:
            return json_write_minified_object((struct FlJsonObject*)value->payload, data);
        case FlJsonType_True:
            data[0] = 't';
            data[1] = 'r';
            data[2] = 'u';
            data[3] = 'e';
            return data + 4;
        case FlJsonType_False:
            data[0] = 'f';
            data[1] = 'a';
            data[2] = 'l';
            data[3] = 's';
            data[4] = 'e';
            return data + 5;
        case FlJsonType_Null:
            data[0] = 'n';
            data[1] = 'u';
            data[2] = 'l';
            data[3] = 'l';
            return data + 4;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_json_write_minified(FlArena* arena, const struct FlJsonValue* value) {
    size_t size = 0;
    char* data = json_null;
    char* data_end = json_null;

    if (json_null == value) {
        return (FlString) { 0 };
    }

    if (json_write_minified_get_value_size(value, &size)) {
        // value was malformed!
        return (FlString) { 0 };
    }

    data = arena_alloc_array(arena, char, size);
    data_end = json_write_minified_value(value, data);

    if (json_null == data_end) {
        // bad chi occurred!
        return (FlString) { 0 };
    }

    return (FlString) { .data = data, .length = size };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

json_weak int json_write_pretty_get_value_size(const struct FlJsonValue* value, size_t depth, size_t indent_size,
                                               size_t newline_size, size_t* size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

json_weak int json_write_pretty_get_array_size(const struct FlJsonArray* array, size_t depth, size_t indent_size,
                                               size_t newline_size, size_t* size);
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_write_pretty_get_array_size(const struct FlJsonArray* array, size_t depth, size_t indent_size,
                                     size_t newline_size, size_t* size) {
    struct FlJsonArrayElement* element;

    *size += 1; /* '['. */

    if (0 < array->length) {
        /* if we have any elements we need to add a newline after our '['. */
        *size += newline_size;

        *size += array->length - 1; /* ','s seperate each element. */

        for (element = array->start; json_null != element; element = element->next) {
            /* each element gets an indent. */
            *size += (depth + 1) * indent_size;

            if (json_write_pretty_get_value_size(element->value, depth + 1, indent_size, newline_size, size)) {
                /* value was malformed! */
                return 1;
            }

            /* each element gets a newline too. */
            *size += newline_size;
        }

        /* since we wrote out some elements, need to add a newline and indentation.
         */
        /* to the trailing ']'. */
        *size += depth * indent_size;
    }

    *size += 1; /* ']'. */

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

json_weak int json_write_pretty_get_object_size(const struct FlJsonObject* object, size_t depth, size_t indent_size,
                                                size_t newline_size, size_t* size);
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_write_pretty_get_object_size(const struct FlJsonObject* object, size_t depth, size_t indent_size,
                                      size_t newline_size, size_t* size) {
    struct FlJsonObjectElement* element;

    *size += 1; /* '{'. */

    if (0 < object->length) {
        *size += newline_size; /* need a newline next. */

        *size += object->length - 1; /* ','s seperate each element. */

        for (element = object->start; json_null != element; element = element->next) {
            /* each element gets an indent and newline. */
            *size += (depth + 1) * indent_size;
            *size += newline_size;

            if (json_write_get_string_size(element->name, size)) {
                /* string was malformed! */
                return 1;
            }

            *size += 3; /* seperate each name/value pair with " : ". */

            if (json_write_pretty_get_value_size(element->value, depth + 1, indent_size, newline_size, size)) {
                /* value was malformed! */
                return 1;
            }
        }

        *size += depth * indent_size;
    }

    *size += 1; /* '}'. */

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

json_weak int json_write_pretty_get_value_size(const struct FlJsonValue* value, size_t depth, size_t indent_size,
                                               size_t newline_size, size_t* size);
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int json_write_pretty_get_value_size(const struct FlJsonValue* value, size_t depth, size_t indent_size,
                                     size_t newline_size, size_t* size) {
    switch (value->type) {
        default:
            /* unknown value type found! */
            return 1;
        case FlJsonType_Number:
            return json_write_get_number_size((struct FlJsonNumber*)value->payload, size);
        case FlJsonType_String:
            return json_write_get_string_size((FlString*)value->payload, size);
        case FlJsonType_Array:
            return json_write_pretty_get_array_size((struct FlJsonArray*)value->payload, depth, indent_size,
                                                    newline_size, size);
        case FlJsonType_Object:
            return json_write_pretty_get_object_size((struct FlJsonObject*)value->payload, depth, indent_size,
                                                     newline_size, size);
        case FlJsonType_True:
            *size += 4; /* the string "true". */
            return 0;
        case FlJsonType_False:
            *size += 5; /* the string "false". */
            return 0;
        case FlJsonType_Null:
            *size += 4; /* the string "null". */
            return 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

json_weak char* json_write_pretty_value(const struct FlJsonValue* value, size_t depth, const char* indent,
                                        const char* newline, char* data);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

json_weak char* json_write_pretty_fixed_array(const struct FlJsonArray* array, size_t depth, const char* indent,
                                              const char* newline, char* data);
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char* json_write_pretty_fixed_array(const struct FlJsonArray* array, size_t depth, const char* indent,
                                    const char* newline, char* data) {
    size_t k, m;
    struct FlJsonArrayElement* element;

    *data++ = '['; /* open the array. */

    if (0 < array->length) {
        for (k = 0; '\0' != newline[k]; k++) {
            *data++ = newline[k];
        }

        for (element = array->start; json_null != element; element = element->next) {
            if (element != array->start) {
                *data++ = ','; /* ','s seperate each element. */

                for (k = 0; '\0' != newline[k]; k++) {
                    *data++ = newline[k];
                }
            }

            for (k = 0; k < depth + 1; k++) {
                for (m = 0; '\0' != indent[m]; m++) {
                    *data++ = indent[m];
                }
            }

            data = json_write_pretty_value(element->value, depth + 1, indent, newline, data);

            if (json_null == data) {
                /* value was malformed! */
                return json_null;
            }
        }

        for (k = 0; '\0' != newline[k]; k++) {
            *data++ = newline[k];
        }

        for (k = 0; k < depth; k++) {
            for (m = 0; '\0' != indent[m]; m++) {
                *data++ = indent[m];
            }
        }
    }

    *data++ = ']'; /* close the array. */

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

json_weak char* json_write_pretty_object(const struct FlJsonObject* object, size_t depth, const char* indent,
                                         const char* newline, char* data);
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char* json_write_pretty_object(const struct FlJsonObject* object, size_t depth, const char* indent, const char* newline,
                               char* data) {
    size_t k, m;
    struct FlJsonObjectElement* element;

    *data++ = '{'; /* open the object. */

    if (0 < object->length) {
        for (k = 0; '\0' != newline[k]; k++) {
            *data++ = newline[k];
        }

        for (element = object->start; json_null != element; element = element->next) {
            if (element != object->start) {
                *data++ = ','; /* ','s seperate each element. */

                for (k = 0; '\0' != newline[k]; k++) {
                    *data++ = newline[k];
                }
            }

            for (k = 0; k < depth + 1; k++) {
                for (m = 0; '\0' != indent[m]; m++) {
                    *data++ = indent[m];
                }
            }

            data = json_write_string(element->name, data);

            if (json_null == data) {
                /* string was malformed! */
                return json_null;
            }

            /* " : "s seperate each name/value pair. */
            *data++ = ' ';
            *data++ = ':';
            *data++ = ' ';

            data = json_write_pretty_value(element->value, depth + 1, indent, newline, data);

            if (json_null == data) {
                /* value was malformed! */
                return json_null;
            }
        }

        for (k = 0; '\0' != newline[k]; k++) {
            *data++ = newline[k];
        }

        for (k = 0; k < depth; k++) {
            for (m = 0; '\0' != indent[m]; m++) {
                *data++ = indent[m];
            }
        }
    }

    *data++ = '}'; /* close the object. */

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

json_weak char* json_write_pretty_value(const struct FlJsonValue* value, size_t depth, const char* indent,
                                        const char* newline, char* data);
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char* json_write_pretty_value(const struct FlJsonValue* value, size_t depth, const char* indent, const char* newline,
                              char* data) {
    switch (value->type) {
        default:
            /* unknown value type found! */
            return json_null;
        case FlJsonType_Number:
            return json_write_number((struct FlJsonNumber*)value->payload, data);
        case FlJsonType_String:
            return json_write_string((FlString*)value->payload, data);
        case FlJsonType_Array:
            return json_write_pretty_fixed_array((struct FlJsonArray*)value->payload, depth, indent, newline, data);
        case FlJsonType_Object:
            return json_write_pretty_object((struct FlJsonObject*)value->payload, depth, indent, newline, data);
        case FlJsonType_True:
            data[0] = 't';
            data[1] = 'r';
            data[2] = 'u';
            data[3] = 'e';
            return data + 4;
        case FlJsonType_False:
            data[0] = 'f';
            data[1] = 'a';
            data[2] = 'l';
            data[3] = 's';
            data[4] = 'e';
            return data + 5;
        case FlJsonType_Null:
            data[0] = 'n';
            data[1] = 'u';
            data[2] = 'l';
            data[3] = 'l';
            return data + 4;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_json_write_pretty(FlArena* arena, const struct FlJsonValue* value, const char* indent,
                              const char* newline) {
    size_t size = 0;
    size_t indent_size = 0;
    size_t newline_size = 0;
    char* data = json_null;
    char* data_end = json_null;

    if (json_null == value) {
        return (FlString) { 0 };
    }

    if (json_null == indent) {
        indent = "  "; // default to two spaces.
    }

    if (json_null == newline) {
        newline = "\n"; // default to linux newlines.
    }

    while ('\0' != indent[indent_size]) {
        ++indent_size; // skip non-null terminating characters.
    }

    while ('\0' != newline[newline_size]) {
        ++newline_size; // skip non-null terminating characters.
    }

    if (json_write_pretty_get_value_size(value, 0, indent_size, newline_size, &size)) {
        // value was malformed!
        return (FlString) { 0 };
    }

    data = arena_alloc_array(arena, char, size);
    data_end = json_write_pretty_value(value, 0, indent, newline, data);

    if (json_null == data_end) {
        // bad chi occurred!
        return (FlString) { 0 };
    }

    return (FlString) { .data = data, .length = size };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSON object property access helpers

FlJsonValue* fl_json_find_property(const FlJsonValue* object, FlString key) {
    if (!object || object->type != FlJsonType_Object) {
        return json_null;
    }

    FlJsonObject* obj = fl_json_value_as_object((FlJsonValue*)object);
    if (!obj) {
        return json_null;
    }
    FlJsonObjectElement* element = obj->start;

    while (element) {
        FlString* name = element->name;
        if (name && fl_string_equals(*name, key)) {
            return element->value;
        }
        element = element->next;
    }
    return json_null;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJsonValue* fl_json_get_path(const FlJsonValue* root, FlString path) {
    if (!root || path.length == 0) {
        return json_null;
    }

    // A missing property, a non-object mid-path or an empty segment fails the lookup.
    const FlJsonValue* current = root;
    u64 start = 0;
    for (u64 i = 0; i <= path.length; i++) {
        if (i == path.length || path.data[i] == '.') {
            const FlString segment = { .data = path.data + start, .length = i - start };
            if (segment.length == 0) {
                return json_null;
            }
            FlJsonValue* next = fl_json_find_property(current, segment);
            if (!next) {
                return json_null;
            }
            current = next;
            start = i + 1;
        }
    }
    return (FlJsonValue*)current;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_json_get_string_prop(const FlJsonValue* object, FlString key) {
    FlJsonValue* prop = fl_json_find_property(object, key);
    if (prop) {
        FlString str = fl_json_value_as_string(prop);
        if (str.length > 0) {
            return str;
        }
    }
    return (FlString) { .data = json_null, .length = 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

unsigned int fl_json_get_u32_prop(const FlJsonValue* object, FlString key) {
    u64 result = fl_json_value_get_u64(fl_json_find_property(object, key));
    return result <= UINT32_MAX ? (unsigned int)result : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

double fl_json_get_f64_prop(const FlJsonValue* object, FlString key) {
    return fl_json_value_get_f64(fl_json_find_property(object, key));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int fl_json_get_bool_prop(const FlJsonValue* object, FlString key) {
    FlJsonValue* prop = fl_json_find_property(object, key);
    return prop ? fl_json_value_is_true(prop) : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJsonValue* fl_json_get_object_prop(const FlJsonValue* object, FlString key) {
    FlJsonValue* prop = fl_json_find_property(object, key);
    return (prop && prop->type == FlJsonType_Object) ? prop : json_null;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJsonArray* fl_json_get_array_prop(const FlJsonValue* object, FlString key) {
    FlJsonValue* prop = fl_json_find_property(object, key);
    return (prop && prop->type == FlJsonType_Array) ? fl_json_value_as_fixed_array(prop) : json_null;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSON position tracking helpers

size_t fl_json_get_line_number(const FlJsonValue* value) {
    if (!fl_json_has_position_info(value)) {
        return 0;
    }

    const JsonValueEx* value_ex = (const JsonValueEx*)value;
    return value_ex->line_no;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

size_t fl_json_get_column_number(const FlJsonValue* value) {
    if (!fl_json_has_position_info(value)) {
        return 0;
    }

    const JsonValueEx* value_ex = (const JsonValueEx*)value;
    return value_ex->row_no + 1; // Convert to 1-based column number
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int fl_json_has_position_info(const FlJsonValue* value) {
    if (!value) {
        return 0;
    }

    const JsonValueEx* value_ex = (const JsonValueEx*)value;

    return (value_ex->line_no > 0) ? 1 : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_json_get_error_description(FlJsonParseError error) {
    switch (error) {
        case FlJsonParseError_None:
            return FL_S("No error");
        case FlJsonParseError_ExpectedCommaOrClosingBracket:
            return FL_S("Expected comma or closing bracket");
        case FlJsonParseError_ExpectedColon:
            return FL_S("Expected colon after object key");
        case FlJsonParseError_ExpectedOpeningQuote:
            return FL_S("Expected opening quote for string");
        case FlJsonParseError_InvalidStringEscapeSequence:
            return FL_S("Invalid string escape sequence");
        case FlJsonParseError_InvalidNumberFormat:
            return FL_S("Invalid number format");
        case FlJsonParseError_InvalidValue:
            return FL_S("Invalid JSON value");
        case FlJsonParseError_PrematureEndOfBuffer:
            return FL_S("Unexpected end of file");
        case FlJsonParseError_InvalidString:
            return FL_S("Invalid string");
        case FlJsonParseError_AllocatorFailed:
            return FL_S("Memory allocation failed");
        case FlJsonParseError_UnexpectedTrailingCharacters:
            return FL_S("Unexpected characters after JSON");
        case FlJsonParseError_TooDeep:
            return FL_S("JSON nesting exceeds maximum depth");
        default:
            return FL_S("Unknown JSON parse error");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static size_t json_count_line_digits(size_t line_no) {
    size_t digits = 1;
    while (line_no >= 10) {
        digits++;
        line_no /= 10;
    }
    return digits;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Format error with full Rust-style context
static FlString json_format_error_with_context(FlArena* arena, FlString error_message, size_t line_no, size_t column,
                                               const char* line_start, const char* line_end, struct FlString file_path,
                                               FlString help_text) {
    FlString line_str = FL_S("");
    if (line_start && line_end && line_end >= line_start) {
        size_t line_length = line_end - line_start;
        line_str = (FlString) { .data = (char*)line_start, .length = line_length };
    }

    size_t line_num_width = json_count_line_digits(line_no);
    if (line_num_width < 2)
        line_num_width = 2;

    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_append_string(sb, FL_S("error: "));
    sb = fl_sb_append_string(sb, error_message);
    sb = fl_sb_append_string(sb, FL_S("\n"));

    if (file_path.length > 0) {
        sb = fl_sb_appendf(sb, " --> %S:%zu:%zu\n", file_path, line_no, column);
    }

    // Separator line (e.g., "  |")
    sb = fl_sb_appendf(sb, "%*s|\n", (int)line_num_width + 1, "");

    sb = fl_sb_appendf(sb, "%*zu | %S\n", (int)line_num_width, line_no, line_str);

    sb = fl_sb_appendf(sb, "%*s | ", (int)line_num_width, "");

    // Add spaces up to error position, preserving tabs
    for_count(i, column - 1) {
        if (i >= line_str.length) {
            break;
        }
        char ch = (line_str.data[i] == '\t') ? '\t' : ' ';
        sb = fl_sb_append_char(sb, ch);
    }

    if (help_text.length > 0) {
        sb = fl_sb_appendf(sb, "^ help: %S", help_text);
    } else {
        sb = fl_sb_append_string(sb, FL_S("^"));
    }

    return fl_sb_to_string(sb);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_json_format_parse_error(FlArena* arena, const FlJsonParseResult* result, const char* source_text,
                                    size_t source_size) {
    if (!arena || !result) {
        return (FlString) { 0 };
    }

    if (result->error == FlJsonParseError_None) {
        return fl_string_copy(arena, FL_S("No error"));
    }

    FlString error_desc = fl_json_get_error_description(result->error);

    if (result->error_line_no == 0) {
        FlStringBuilder sb = fl_sb_create(arena);
        sb = fl_sb_appendf(sb, "JSON Parse Error: %S", error_desc);
        return fl_sb_to_string(sb);
    }

    const char* line_start = nullptr;
    const char* line_end = nullptr;

    if (source_text && source_size > 0) {
        size_t current_line = 1;
        const char* current_pos = source_text;
        const char* current_line_start = source_text;
        const char* source_end = source_text + source_size;

        while (current_pos < source_end && current_line < result->error_line_no) {
            if (*current_pos == '\n') {
                current_line++;
                current_line_start = current_pos + 1;
            }
            current_pos++;
        }

        if (current_line == result->error_line_no) {
            line_start = current_line_start;

            line_end = line_start;
            while (line_end < source_end && *line_end != '\n') {
                line_end++;
            }
        }
    }

    if (line_start && line_end) {
        return json_format_error_with_context(arena, error_desc, result->error_line_no,
                                              result->error_row_no + 1, // Convert to 1-based column
                                              line_start, line_end, (FlString) { 0 }, FL_S(""));
    }

    size_t line_num_digits = json_count_line_digits(result->error_line_no);

    FlStringBuilder sb = fl_sb_create(arena);
    sb = fl_sb_appendf(sb, "JSON Parse Error: %S\n", error_desc);
    sb = fl_sb_appendf(sb, "%*s |\n", (int)line_num_digits, "");
    sb = fl_sb_appendf(sb, "%*zu | (error at line %zu, column %zu)\n", (int)line_num_digits, result->error_line_no,
                       result->error_line_no, result->error_row_no + 1);
    sb = fl_sb_appendf(sb, "%*s |", (int)line_num_digits, "");
    return fl_sb_to_string(sb);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_json_format_value_error(FlArena* arena, const FlJsonValue* value, FlString error_message,
                                    struct FlString file_path, FlString help_text) {
    if (!arena || !value || error_message.length == 0) {
        return (FlString) { 0 };
    }

    const JsonValueEx* value_ex = (const JsonValueEx*)value;

    if (!fl_json_has_position_info(value)) {
        FlStringBuilder sb = fl_sb_create(arena);
        sb = fl_sb_appendf(sb, "error: %S", error_message);
        return fl_sb_to_string(sb);
    }

    // If line pointers are nullptr, we can't show source context
    const char* line_start = value_ex->line_start;
    const char* line_end = value_ex->line_end;

    return json_format_error_with_context(arena, error_message, value_ex->line_no,
                                          value_ex->row_no + 1, // Convert to 1-based
                                          line_start, line_end, file_path, help_text);
}

#if COMPILER_CLANG
#pragma clang diagnostic pop
#elif COMPILER_MSVC
#pragma warning(pop)
#endif
