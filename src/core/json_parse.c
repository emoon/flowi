#include "arena.h"
#include "json.h"
#include "string.h"

#include <string.h>

#define JSON_MAX_DEPTH 256

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct JsonParser {
    FlArena* arena;
    const char* src;
    size_t size;
    size_t offset;
    size_t line_no;
    size_t line_start;
    size_t depth;
    bool json5;
    FlJsonParseResult failure;
} JsonParser;

typedef struct JsonSink {
    char* dst;
    size_t length;
} JsonSink;

typedef struct JsonList {
    char close;
    bool global;
    size_t count;
} JsonList;

typedef enum JsonItem {
    JsonItem_Next,
    JsonItem_Done,
    JsonItem_Failed,
} JsonItem;

static FlJsonValue* json_parse_value(JsonParser* p);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_at_end(const JsonParser* p) {
    return p->offset >= p->size;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char json_peek_at(const JsonParser* p, size_t ahead) {
    return p->offset + ahead < p->size ? p->src[p->offset + ahead] : '\0';
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char json_peek(const JsonParser* p) {
    return json_peek_at(p, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_advance(JsonParser* p) {
    if (p->src[p->offset] == '\n') {
        p->line_no++;
        p->line_start = p->offset + 1;
    }
    p->offset++;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_advance_by(JsonParser* p, size_t count) {
    for (size_t i = 0; i < count; i++) {
        json_advance(p);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_fail(JsonParser* p, FlJsonParseError error) {
    p->failure.error = error;
    p->failure.error_offset = p->offset;
    p->failure.error_line_no = p->line_no;
    p->failure.error_row_no = p->offset - p->line_start + 1;
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* json_fail_value(JsonParser* p, FlJsonParseError error) {
    json_fail(p, error);
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_char_in(char c, const char* set) {
    return c != '\0' && strchr(set, c) != nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_is_space(char c) {
    return json_char_in(c, " \t\r\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_is_digit(char c) {
    return c >= '0' && c <= '9';
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_is_key_char(char c) {
    return json_is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_skip_line_comment(JsonParser* p) {
    while (!json_at_end(p)) {
        const char c = json_peek(p);
        json_advance(p);
        if (c == '\n') {
            return;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_skip_block_comment(JsonParser* p) {
    json_advance_by(p, 2);
    while (!json_at_end(p)) {
        if (json_peek(p) == '*' && json_peek_at(p, 1) == '/') {
            json_advance_by(p, 2);
            return;
        }
        json_advance(p);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_skip_comment(JsonParser* p) {
    if (json_peek(p) != '/') {
        return false;
    }
    const char next = json_peek_at(p, 1);
    if (next == '/') {
        json_skip_line_comment(p);
        return true;
    }
    if (next == '*') {
        json_skip_block_comment(p);
        return true;
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_skip_trivia(JsonParser* p) {
    while (!json_at_end(p)) {
        if (json_is_space(json_peek(p))) {
            json_advance(p);
        } else if (!p->json5 || !json_skip_comment(p)) {
            return;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_match_word(JsonParser* p, const char* word) {
    const size_t length = strlen(word);
    if (p->size - p->offset < length || memcmp(p->src + p->offset, word, length) != 0) {
        return false;
    }
    json_advance_by(p, length);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* json_new_value(JsonParser* p, FlJsonType type, void* payload) {
    FlJsonValue* value = arena_alloc(p->arena, FlJsonValue);
    value->type = type;
    value->payload = payload;
    return value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char* json_copy_text(JsonParser* p, const char* text, size_t length) {
    char* copy = arena_alloc_array(p->arena, char, length + 1);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_check_number_end(JsonParser* p) {
    if (json_at_end(p) || json_char_in(json_peek(p), " \t\r\n},]")) {
        return true;
    }
    return json_fail(p, FlJsonParseError_InvalidNumberFormat);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_scan_hex(JsonParser* p) {
    json_advance_by(p, 2);
    if (json_hex_digit_value(json_peek(p)) < 0) {
        return json_fail(p, FlJsonParseError_InvalidNumberFormat);
    }
    while (json_hex_digit_value(json_peek(p)) >= 0) {
        json_advance(p);
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_skip_digits(JsonParser* p) {
    while (json_is_digit(json_peek(p))) {
        json_advance(p);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_scan_integer(JsonParser* p) {
    if (json_peek(p) != '0') {
        json_skip_digits(p);
        return true;
    }
    json_advance(p);
    if (json_is_digit(json_peek(p))) {
        return json_fail(p, FlJsonParseError_InvalidNumberFormat);
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_scan_fraction(JsonParser* p, bool had_integer) {
    if (json_peek(p) != '.') {
        return true;
    }
    json_advance(p);
    if (json_is_digit(json_peek(p))) {
        json_skip_digits(p);
        return true;
    }
    return (p->json5 && had_integer) || json_fail(p, FlJsonParseError_InvalidNumberFormat);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_scan_exponent(JsonParser* p) {
    if (json_peek(p) != 'e' && json_peek(p) != 'E') {
        return true;
    }
    json_advance(p);
    if (json_peek(p) == '-' || json_peek(p) == '+') {
        json_advance(p);
    }
    if (!json_is_digit(json_peek(p))) {
        return json_fail(p, FlJsonParseError_InvalidNumberFormat);
    }
    json_skip_digits(p);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_scan_decimal(JsonParser* p) {
    const size_t start = p->offset;
    if (!json_scan_integer(p)) {
        return false;
    }
    const bool had_integer = p->offset > start;
    if (!json_scan_fraction(p, had_integer)) {
        return false;
    }
    if (p->offset == start) {
        return json_fail(p, FlJsonParseError_InvalidNumberFormat);
    }
    return json_scan_exponent(p);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_match_special(JsonParser* p) {
    return json_match_word(p, "Infinity") || json_match_word(p, "NaN");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_scan_special(JsonParser* p) {
    if (!json_match_special(p)) {
        return false;
    }
    if (json_char_in(json_peek(p), "0123456789eE")) {
        return json_fail(p, FlJsonParseError_InvalidNumberFormat);
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_at_hex_prefix(const JsonParser* p) {
    return p->json5 && json_peek(p) == '0' && json_char_in(json_peek_at(p, 1), "xX");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_scan_signed_number(JsonParser* p) {
    if (json_char_in(json_peek(p), p->json5 ? "+-" : "-")) {
        json_advance(p);
    }
    if (p->json5 && json_char_in(json_peek(p), "IN")) {
        return json_scan_special(p) || json_fail(p, FlJsonParseError_InvalidNumberFormat);
    }
    return json_scan_decimal(p);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_scan_number(JsonParser* p) {
    const bool scanned = json_at_hex_prefix(p) ? json_scan_hex(p) : json_scan_signed_number(p);
    return scanned && json_check_number_end(p);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* json_parse_number_value(JsonParser* p) {
    const size_t start = p->offset;
    if (!json_scan_number(p)) {
        return nullptr;
    }
    FlJsonNumber* number = arena_alloc(p->arena, FlJsonNumber);
    number->number_size = p->offset - start;
    number->number = json_copy_text(p, p->src + start, number->number_size);
    return json_new_value(p, FlJsonType_Number, number);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_emit(JsonSink* sink, char c) {
    if (sink->dst) {
        sink->dst[sink->length] = c;
    }
    sink->length++;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_emit_continuation(JsonSink* sink, u32 codepoint, u32 shift) {
    json_emit(sink, (char)(0x80u | ((codepoint >> shift) & 0x3fu)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_emit_utf8(JsonSink* sink, u32 codepoint) {
    if (codepoint <= 0x7f) {
        json_emit(sink, (char)codepoint);
    } else if (codepoint <= 0x7ff) {
        json_emit(sink, (char)(0xc0u | (codepoint >> 6)));
        json_emit_continuation(sink, codepoint, 0);
    } else if (codepoint <= 0xffff) {
        json_emit(sink, (char)(0xe0u | (codepoint >> 12)));
        json_emit_continuation(sink, codepoint, 6);
        json_emit_continuation(sink, codepoint, 0);
    } else {
        json_emit(sink, (char)(0xf0u | (codepoint >> 18)));
        json_emit_continuation(sink, codepoint, 12);
        json_emit_continuation(sink, codepoint, 6);
        json_emit_continuation(sink, codepoint, 0);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_read_hex4(JsonParser* p, u32* out) {
    u32 value = 0;
    for (size_t i = 0; i < 4; i++) {
        const int digit = json_hex_digit_value(json_peek_at(p, i));
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | (u32)digit;
    }
    json_advance_by(p, 4);
    *out = value;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_read_low_surrogate(JsonParser* p, u32* out) {
    if (json_peek(p) != '\\' || json_peek_at(p, 1) != 'u') {
        return false;
    }
    json_advance_by(p, 2);
    return json_read_hex4(p, out) && *out >= 0xdc00 && *out <= 0xdfff;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_decode_unicode(JsonParser* p, JsonSink* sink) {
    u32 codepoint;
    if (!json_read_hex4(p, &codepoint)) {
        return json_fail(p, FlJsonParseError_InvalidStringEscapeSequence);
    }
    if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
        u32 low;
        if (!json_read_low_surrogate(p, &low)) {
            return json_fail(p, FlJsonParseError_InvalidStringEscapeSequence);
        }
        codepoint = 0x10000u + ((codepoint - 0xd800u) << 10) + (low - 0xdc00u);
    } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
        return json_fail(p, FlJsonParseError_InvalidStringEscapeSequence);
    }
    json_emit_utf8(sink, codepoint);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char json_simple_escape(char c) {
    if (c == '/') {
        return c;
    }
    const char* code = c != '\0' ? strchr(JSON_ESCAPE_CODES, c) : nullptr;
    return code ? JSON_ESCAPE_VALUES[code - JSON_ESCAPE_CODES] : '\0';
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_decode_escape(JsonParser* p, JsonSink* sink) {
    json_advance(p);
    if (json_at_end(p)) {
        return json_fail(p, FlJsonParseError_PrematureEndOfBuffer);
    }
    const char c = json_peek(p);
    json_advance(p);
    if (c == 'u') {
        return json_decode_unicode(p, sink);
    }
    const char decoded = json_simple_escape(c);
    if (decoded == '\0') {
        return json_fail(p, FlJsonParseError_InvalidStringEscapeSequence);
    }
    json_emit(sink, decoded);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_decode_plain(JsonParser* p, JsonSink* sink) {
    const char c = json_peek(p);
    if (c == '\0' || c == '\t') {
        return json_fail(p, FlJsonParseError_InvalidString);
    }
    if (json_char_in(c, "\r\n") && !p->json5) {
        return json_fail(p, FlJsonParseError_InvalidStringEscapeSequence);
    }
    json_emit(sink, c);
    json_advance(p);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_decode_string(JsonParser* p, JsonSink* sink) {
    const char quote = json_peek(p);
    json_advance(p);
    while (!json_at_end(p) && json_peek(p) != quote) {
        const bool decoded = json_peek(p) == '\\' ? json_decode_escape(p, sink) : json_decode_plain(p, sink);
        if (!decoded) {
            return false;
        }
    }
    if (json_at_end(p)) {
        return json_fail(p, FlJsonParseError_PrematureEndOfBuffer);
    }
    json_advance(p);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_at_string_start(const JsonParser* p) {
    return json_peek(p) == '"' || (p->json5 && json_peek(p) == '\'');
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_parse_string(JsonParser* p, FlString* out) {
    if (!json_at_string_start(p)) {
        return json_fail(p, FlJsonParseError_ExpectedOpeningQuote);
    }
    const JsonParser start = *p;
    JsonSink measure = { 0 };
    if (!json_decode_string(p, &measure)) {
        return false;
    }
    *p = start;
    JsonSink write = { .dst = arena_alloc_array(p->arena, char, measure.length + 1) };
    json_decode_string(p, &write);
    write.dst[write.length] = '\0';
    *out = (FlString) { .data = write.dst, .length = write.length, .is_static = 0 };
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* json_parse_string_value(JsonParser* p) {
    FlString* string = arena_alloc(p->arena, FlString);
    if (!json_parse_string(p, string)) {
        return nullptr;
    }
    return json_new_value(p, FlJsonType_String, string);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_parse_bare_key(JsonParser* p, FlString* out) {
    const size_t start = p->offset;
    while (json_is_key_char(json_peek(p))) {
        json_advance(p);
    }
    if (p->offset == start) {
        return json_fail(p, FlJsonParseError_InvalidString);
    }
    const size_t length = p->offset - start;
    *out = (FlString) { .data = json_copy_text(p, p->src + start, length), .length = length, .is_static = 0 };
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_parse_key(JsonParser* p, FlString* out) {
    if (!p->json5 || json_at_string_start(p)) {
        return json_parse_string(p, out);
    }
    return json_parse_bare_key(p, out);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* json_parse_literal(JsonParser* p) {
    if (json_match_word(p, "true")) {
        return json_new_value(p, FlJsonType_True, nullptr);
    }
    if (json_match_word(p, "false")) {
        return json_new_value(p, FlJsonType_False, nullptr);
    }
    if (json_match_word(p, "null")) {
        return json_new_value(p, FlJsonType_Null, nullptr);
    }
    return json_fail_value(p, FlJsonParseError_InvalidValue);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static JsonItem json_list_fail(JsonParser* p, FlJsonParseError error) {
    json_fail(p, error);
    return JsonItem_Failed;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static JsonItem json_list_check_end(JsonParser* p, const JsonList* list) {
    if (json_at_end(p)) {
        return list->global ? JsonItem_Done : json_list_fail(p, FlJsonParseError_PrematureEndOfBuffer);
    }
    if (!list->global && json_peek(p) == list->close) {
        json_advance(p);
        return JsonItem_Done;
    }
    return JsonItem_Next;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static JsonItem json_list_next(JsonParser* p, JsonList* list) {
    json_skip_trivia(p);
    const JsonItem state = json_list_check_end(p, list);
    if (state != JsonItem_Next || list->count == 0) {
        return state;
    }
    if (json_peek(p) != ',') {
        return json_list_fail(p, FlJsonParseError_ExpectedCommaOrClosingBracket);
    }
    json_advance(p);
    json_skip_trivia(p);
    return p->json5 ? json_list_check_end(p, list) : JsonItem_Next;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* json_parse_array(JsonParser* p) {
    FlJsonArray* array = arena_alloc_zero(p->arena, FlJsonArray);
    FlJsonValue* value = json_new_value(p, FlJsonType_Array, array);
    FlJsonArrayElement** tail = &array->start;
    JsonList list = { .close = ']' };
    json_advance(p);
    JsonItem item;
    while ((item = json_list_next(p, &list)) == JsonItem_Next) {
        FlJsonArrayElement* element = arena_alloc_zero(p->arena, FlJsonArrayElement);
        element->value = json_parse_value(p);
        if (!element->value) {
            return nullptr;
        }
        *tail = element;
        tail = &element->next;
        list.count++;
    }
    array->length = list.count;
    return item == JsonItem_Done ? value : nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_parse_member(JsonParser* p, FlJsonObjectElement* element) {
    element->name = arena_alloc(p->arena, FlString);
    if (!json_parse_key(p, element->name)) {
        return false;
    }
    json_skip_trivia(p);
    if (json_peek(p) != ':') {
        return json_fail(p, json_at_end(p) ? FlJsonParseError_PrematureEndOfBuffer : FlJsonParseError_ExpectedColon);
    }
    json_advance(p);
    element->value = json_parse_value(p);
    return element->value != nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* json_parse_object(JsonParser* p, bool global) {
    FlJsonObject* object = arena_alloc_zero(p->arena, FlJsonObject);
    FlJsonValue* value = json_new_value(p, FlJsonType_Object, object);
    FlJsonObjectElement** tail = &object->start;
    JsonList list = { .close = '}', .global = global };
    if (!global) {
        json_advance(p);
    }
    JsonItem item;
    while ((item = json_list_next(p, &list)) == JsonItem_Next) {
        FlJsonObjectElement* element = arena_alloc_zero(p->arena, FlJsonObjectElement);
        if (!json_parse_member(p, element)) {
            return nullptr;
        }
        *tail = element;
        tail = &element->next;
        list.count++;
    }
    object->length = list.count;
    return item == JsonItem_Done ? value : nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* json_parse_container(JsonParser* p, FlJsonType type, bool global) {
    if (p->depth >= JSON_MAX_DEPTH) {
        return json_fail_value(p, FlJsonParseError_TooDeep);
    }
    p->depth++;
    FlJsonValue* value = type == FlJsonType_Array ? json_parse_array(p) : json_parse_object(p, global);
    p->depth--;
    return value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_at_number_start(const JsonParser* p) {
    const char c = json_peek(p);
    return json_is_digit(c) || c == '-' || (p->json5 && json_char_in(c, "+.IN"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* json_parse_value(JsonParser* p) {
    json_skip_trivia(p);
    if (json_at_end(p)) {
        return json_fail_value(p, FlJsonParseError_PrematureEndOfBuffer);
    }
    if (json_at_string_start(p)) {
        return json_parse_string_value(p);
    }
    if (json_peek(p) == '{') {
        return json_parse_container(p, FlJsonType_Object, false);
    }
    if (json_peek(p) == '[') {
        return json_parse_container(p, FlJsonType_Array, false);
    }
    if (json_at_number_start(p)) {
        return json_parse_number_value(p);
    }
    return json_parse_literal(p);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* json_parse_document(JsonParser* p) {
    if (p->json5) {
        json_skip_trivia(p);
        if (json_peek(p) != '{') {
            return json_parse_container(p, FlJsonType_Object, true);
        }
    }
    return json_parse_value(p);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_finish_document(JsonParser* p) {
    json_skip_trivia(p);
    return json_at_end(p) || json_fail(p, FlJsonParseError_UnexpectedTrailingCharacters);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJsonValue* fl_json_parse_with_flags(FlArena* arena, FlString src, size_t flags_bitset, FlJsonParseResult* result) {
    if (result) {
        *result = (FlJsonParseResult) { 0 };
    }
    if (!src.data) {
        return nullptr;
    }
    JsonParser parser = {
        .arena = arena,
        .src = src.data,
        .size = src.length,
        .line_no = 1,
        .json5 = (flags_bitset & FlJsonParseFlags_AllowJson5) != 0,
    };
    const void* mark = arena_pos_type(arena, u8);
    FlJsonValue* root = json_parse_document(&parser);
    if (root && !json_finish_document(&parser)) {
        root = nullptr;
    }
    if (!root) {
        arena_rewind_to(arena, mark);
        if (result) {
            *result = parser.failure;
        }
    }
    return root;
}
