#include "arena.h"
#include "json.h"
#include "string.h"

#include <string.h>

#define JSON_MAX_DEPTH 256

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

static FlJsonValue* parse_value(JsonParser* p);

static bool at_end(const JsonParser* p) {
    return p->offset >= p->size;
}

static char peek_at(const JsonParser* p, size_t ahead) {
    return p->offset + ahead < p->size ? p->src[p->offset + ahead] : '\0';
}

static char peek(const JsonParser* p) {
    return peek_at(p, 0);
}

static void advance(JsonParser* p) {
    if (p->src[p->offset] == '\n') {
        p->line_no++;
        p->line_start = p->offset + 1;
    }
    p->offset++;
}

static void advance_by(JsonParser* p, size_t count) {
    for (size_t i = 0; i < count; i++) {
        advance(p);
    }
}

static bool fail(JsonParser* p, FlJsonParseError error) {
    p->failure.error = error;
    p->failure.error_offset = p->offset;
    p->failure.error_line_no = p->line_no;
    p->failure.error_row_no = p->offset - p->line_start + 1;
    return false;
}

static void* fail_value(JsonParser* p, FlJsonParseError error) {
    fail(p, error);
    return nullptr;
}

static bool char_in(char c, const char* set) {
    return c != '\0' && strchr(set, c) != nullptr;
}

static bool is_space(char c) {
    return char_in(c, " \t\r\n");
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_key_char(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static void skip_line_comment(JsonParser* p) {
    while (!at_end(p)) {
        const char c = peek(p);
        advance(p);
        if (c == '\n') {
            return;
        }
    }
}

static void skip_block_comment(JsonParser* p) {
    advance_by(p, 2);
    while (!at_end(p)) {
        if (peek(p) == '*' && peek_at(p, 1) == '/') {
            advance_by(p, 2);
            return;
        }
        advance(p);
    }
}

static bool skip_comment(JsonParser* p) {
    if (peek(p) != '/') {
        return false;
    }
    const char next = peek_at(p, 1);
    if (next == '/') {
        skip_line_comment(p);
        return true;
    }
    if (next == '*') {
        skip_block_comment(p);
        return true;
    }
    return false;
}

static void skip_trivia(JsonParser* p) {
    while (!at_end(p)) {
        if (is_space(peek(p))) {
            advance(p);
        } else if (!p->json5 || !skip_comment(p)) {
            return;
        }
    }
}

static bool match_word(JsonParser* p, const char* word) {
    const size_t length = strlen(word);
    if (p->size - p->offset < length || memcmp(p->src + p->offset, word, length) != 0) {
        return false;
    }
    advance_by(p, length);
    return true;
}

static FlJsonValue* new_value(JsonParser* p, FlJsonType type, void* payload) {
    FlJsonValue* value = arena_alloc(p->arena, FlJsonValue);
    value->type = type;
    value->payload = payload;
    return value;
}

static char* copy_text(JsonParser* p, const char* text, size_t length) {
    char* copy = arena_alloc_array(p->arena, char, length + 1);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static bool check_number_end(JsonParser* p) {
    if (at_end(p) || char_in(peek(p), " \t\r\n},]")) {
        return true;
    }
    return fail(p, FlJsonParseError_InvalidNumberFormat);
}

static bool scan_hex(JsonParser* p) {
    advance_by(p, 2);
    if (json_hex_digit_value(peek(p)) < 0) {
        return fail(p, FlJsonParseError_InvalidNumberFormat);
    }
    while (json_hex_digit_value(peek(p)) >= 0) {
        advance(p);
    }
    return true;
}

static void skip_digits(JsonParser* p) {
    while (is_digit(peek(p))) {
        advance(p);
    }
}

static bool scan_integer(JsonParser* p) {
    if (peek(p) != '0') {
        skip_digits(p);
        return true;
    }
    advance(p);
    if (is_digit(peek(p))) {
        return fail(p, FlJsonParseError_InvalidNumberFormat);
    }
    return true;
}

static bool scan_fraction(JsonParser* p, bool had_integer) {
    if (peek(p) != '.') {
        return true;
    }
    advance(p);
    if (is_digit(peek(p))) {
        skip_digits(p);
        return true;
    }
    return (p->json5 && had_integer) || fail(p, FlJsonParseError_InvalidNumberFormat);
}

static bool scan_exponent(JsonParser* p) {
    if (peek(p) != 'e' && peek(p) != 'E') {
        return true;
    }
    advance(p);
    if (peek(p) == '-' || peek(p) == '+') {
        advance(p);
    }
    if (!is_digit(peek(p))) {
        return fail(p, FlJsonParseError_InvalidNumberFormat);
    }
    skip_digits(p);
    return true;
}

static bool scan_decimal(JsonParser* p) {
    const size_t start = p->offset;
    if (!scan_integer(p)) {
        return false;
    }
    const bool had_integer = p->offset > start;
    if (!scan_fraction(p, had_integer)) {
        return false;
    }
    if (p->offset == start) {
        return fail(p, FlJsonParseError_InvalidNumberFormat);
    }
    return scan_exponent(p);
}

static bool match_special(JsonParser* p) {
    return match_word(p, "Infinity") || match_word(p, "NaN");
}

static bool scan_special(JsonParser* p) {
    if (!match_special(p)) {
        return false;
    }
    if (char_in(peek(p), "0123456789eE")) {
        return fail(p, FlJsonParseError_InvalidNumberFormat);
    }
    return true;
}

static bool at_hex_prefix(const JsonParser* p) {
    return p->json5 && peek(p) == '0' && char_in(peek_at(p, 1), "xX");
}

static bool scan_signed_number(JsonParser* p) {
    if (char_in(peek(p), p->json5 ? "+-" : "-")) {
        advance(p);
    }
    if (p->json5 && char_in(peek(p), "IN")) {
        return scan_special(p) || fail(p, FlJsonParseError_InvalidNumberFormat);
    }
    return scan_decimal(p);
}

static bool scan_number(JsonParser* p) {
    const bool scanned = at_hex_prefix(p) ? scan_hex(p) : scan_signed_number(p);
    return scanned && check_number_end(p);
}

static FlJsonValue* parse_number_value(JsonParser* p) {
    const size_t start = p->offset;
    if (!scan_number(p)) {
        return nullptr;
    }
    FlJsonNumber* number = arena_alloc(p->arena, FlJsonNumber);
    number->number_size = p->offset - start;
    number->number = copy_text(p, p->src + start, number->number_size);
    return new_value(p, FlJsonType_Number, number);
}

static void emit(JsonSink* sink, char c) {
    if (sink->dst) {
        sink->dst[sink->length] = c;
    }
    sink->length++;
}

static void emit_continuation(JsonSink* sink, u32 codepoint, u32 shift) {
    emit(sink, (char)(0x80u | ((codepoint >> shift) & 0x3fu)));
}

static void emit_utf8(JsonSink* sink, u32 codepoint) {
    if (codepoint <= 0x7f) {
        emit(sink, (char)codepoint);
    } else if (codepoint <= 0x7ff) {
        emit(sink, (char)(0xc0u | (codepoint >> 6)));
        emit_continuation(sink, codepoint, 0);
    } else if (codepoint <= 0xffff) {
        emit(sink, (char)(0xe0u | (codepoint >> 12)));
        emit_continuation(sink, codepoint, 6);
        emit_continuation(sink, codepoint, 0);
    } else {
        emit(sink, (char)(0xf0u | (codepoint >> 18)));
        emit_continuation(sink, codepoint, 12);
        emit_continuation(sink, codepoint, 6);
        emit_continuation(sink, codepoint, 0);
    }
}

static bool read_hex4(JsonParser* p, u32* out) {
    u32 value = 0;
    for (size_t i = 0; i < 4; i++) {
        const int digit = json_hex_digit_value(peek_at(p, i));
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | (u32)digit;
    }
    advance_by(p, 4);
    *out = value;
    return true;
}

static bool read_low_surrogate(JsonParser* p, u32* out) {
    if (peek(p) != '\\' || peek_at(p, 1) != 'u') {
        return false;
    }
    advance_by(p, 2);
    return read_hex4(p, out) && *out >= 0xdc00 && *out <= 0xdfff;
}

static bool decode_unicode(JsonParser* p, JsonSink* sink) {
    u32 codepoint;
    if (!read_hex4(p, &codepoint)) {
        return fail(p, FlJsonParseError_InvalidStringEscapeSequence);
    }
    if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
        u32 low;
        if (!read_low_surrogate(p, &low)) {
            return fail(p, FlJsonParseError_InvalidStringEscapeSequence);
        }
        codepoint = 0x10000u + ((codepoint - 0xd800u) << 10) + (low - 0xdc00u);
    } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
        return fail(p, FlJsonParseError_InvalidStringEscapeSequence);
    }
    emit_utf8(sink, codepoint);
    return true;
}

static char simple_escape(char c) {
    if (c == '/') {
        return c;
    }
    const char* code = c != '\0' ? strchr(JSON_ESCAPE_CODES, c) : nullptr;
    return code ? JSON_ESCAPE_VALUES[code - JSON_ESCAPE_CODES] : '\0';
}

static bool decode_escape(JsonParser* p, JsonSink* sink) {
    advance(p);
    if (at_end(p)) {
        return fail(p, FlJsonParseError_PrematureEndOfBuffer);
    }
    const char c = peek(p);
    advance(p);
    if (c == 'u') {
        return decode_unicode(p, sink);
    }
    const char decoded = simple_escape(c);
    if (decoded == '\0') {
        return fail(p, FlJsonParseError_InvalidStringEscapeSequence);
    }
    emit(sink, decoded);
    return true;
}

static bool decode_plain(JsonParser* p, JsonSink* sink) {
    const char c = peek(p);
    if (c == '\0' || c == '\t') {
        return fail(p, FlJsonParseError_InvalidString);
    }
    if (char_in(c, "\r\n") && !p->json5) {
        return fail(p, FlJsonParseError_InvalidStringEscapeSequence);
    }
    emit(sink, c);
    advance(p);
    return true;
}

static bool decode_string(JsonParser* p, JsonSink* sink) {
    const char quote = peek(p);
    advance(p);
    while (!at_end(p) && peek(p) != quote) {
        const bool decoded = peek(p) == '\\' ? decode_escape(p, sink) : decode_plain(p, sink);
        if (!decoded) {
            return false;
        }
    }
    if (at_end(p)) {
        return fail(p, FlJsonParseError_PrematureEndOfBuffer);
    }
    advance(p);
    return true;
}

static bool at_string_start(const JsonParser* p) {
    return peek(p) == '"' || (p->json5 && peek(p) == '\'');
}

static bool parse_string(JsonParser* p, FlString* out) {
    if (!at_string_start(p)) {
        return fail(p, FlJsonParseError_ExpectedOpeningQuote);
    }
    const JsonParser start = *p;
    JsonSink measure = { 0 };
    if (!decode_string(p, &measure)) {
        return false;
    }
    *p = start;
    JsonSink write = { .dst = arena_alloc_array(p->arena, char, measure.length + 1) };
    decode_string(p, &write);
    write.dst[write.length] = '\0';
    *out = (FlString) { .data = write.dst, .length = write.length, .is_static = 0 };
    return true;
}

static FlJsonValue* parse_string_value(JsonParser* p) {
    FlString* string = arena_alloc(p->arena, FlString);
    if (!parse_string(p, string)) {
        return nullptr;
    }
    return new_value(p, FlJsonType_String, string);
}

static bool parse_bare_key(JsonParser* p, FlString* out) {
    const size_t start = p->offset;
    while (is_key_char(peek(p))) {
        advance(p);
    }
    if (p->offset == start) {
        return fail(p, FlJsonParseError_InvalidString);
    }
    const size_t length = p->offset - start;
    *out = (FlString) { .data = copy_text(p, p->src + start, length), .length = length, .is_static = 0 };
    return true;
}

static bool parse_key(JsonParser* p, FlString* out) {
    if (!p->json5 || at_string_start(p)) {
        return parse_string(p, out);
    }
    return parse_bare_key(p, out);
}

static FlJsonValue* parse_literal(JsonParser* p) {
    if (match_word(p, "true")) {
        return new_value(p, FlJsonType_True, nullptr);
    }
    if (match_word(p, "false")) {
        return new_value(p, FlJsonType_False, nullptr);
    }
    if (match_word(p, "null")) {
        return new_value(p, FlJsonType_Null, nullptr);
    }
    return fail_value(p, FlJsonParseError_InvalidValue);
}

static JsonItem list_fail(JsonParser* p, FlJsonParseError error) {
    fail(p, error);
    return JsonItem_Failed;
}

static JsonItem list_check_end(JsonParser* p, const JsonList* list) {
    if (at_end(p)) {
        return list->global ? JsonItem_Done : list_fail(p, FlJsonParseError_PrematureEndOfBuffer);
    }
    if (!list->global && peek(p) == list->close) {
        advance(p);
        return JsonItem_Done;
    }
    return JsonItem_Next;
}

static JsonItem list_next(JsonParser* p, JsonList* list) {
    skip_trivia(p);
    const JsonItem state = list_check_end(p, list);
    if (state != JsonItem_Next || list->count == 0) {
        return state;
    }
    if (peek(p) != ',') {
        return list_fail(p, FlJsonParseError_ExpectedCommaOrClosingBracket);
    }
    advance(p);
    skip_trivia(p);
    return p->json5 ? list_check_end(p, list) : JsonItem_Next;
}

static FlJsonValue* parse_array(JsonParser* p) {
    FlJsonArray* array = arena_alloc_zero(p->arena, FlJsonArray);
    FlJsonValue* value = new_value(p, FlJsonType_Array, array);
    FlJsonArrayElement** tail = &array->start;
    JsonList list = { .close = ']' };
    advance(p);
    JsonItem item;
    while ((item = list_next(p, &list)) == JsonItem_Next) {
        FlJsonArrayElement* element = arena_alloc_zero(p->arena, FlJsonArrayElement);
        element->value = parse_value(p);
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

static bool parse_member(JsonParser* p, FlJsonObjectElement* element) {
    element->name = arena_alloc(p->arena, FlString);
    if (!parse_key(p, element->name)) {
        return false;
    }
    skip_trivia(p);
    if (peek(p) != ':') {
        return fail(p, at_end(p) ? FlJsonParseError_PrematureEndOfBuffer : FlJsonParseError_ExpectedColon);
    }
    advance(p);
    element->value = parse_value(p);
    return element->value != nullptr;
}

static FlJsonValue* parse_object(JsonParser* p, bool global) {
    FlJsonObject* object = arena_alloc_zero(p->arena, FlJsonObject);
    FlJsonValue* value = new_value(p, FlJsonType_Object, object);
    FlJsonObjectElement** tail = &object->start;
    JsonList list = { .close = '}', .global = global };
    if (!global) {
        advance(p);
    }
    JsonItem item;
    while ((item = list_next(p, &list)) == JsonItem_Next) {
        FlJsonObjectElement* element = arena_alloc_zero(p->arena, FlJsonObjectElement);
        if (!parse_member(p, element)) {
            return nullptr;
        }
        *tail = element;
        tail = &element->next;
        list.count++;
    }
    object->length = list.count;
    return item == JsonItem_Done ? value : nullptr;
}

static FlJsonValue* parse_container(JsonParser* p, FlJsonType type, bool global) {
    if (p->depth >= JSON_MAX_DEPTH) {
        return fail_value(p, FlJsonParseError_TooDeep);
    }
    p->depth++;
    FlJsonValue* value = type == FlJsonType_Array ? parse_array(p) : parse_object(p, global);
    p->depth--;
    return value;
}

static bool at_number_start(const JsonParser* p) {
    const char c = peek(p);
    return is_digit(c) || c == '-' || (p->json5 && char_in(c, "+.IN"));
}

static FlJsonValue* parse_value(JsonParser* p) {
    skip_trivia(p);
    if (at_end(p)) {
        return fail_value(p, FlJsonParseError_PrematureEndOfBuffer);
    }
    if (at_string_start(p)) {
        return parse_string_value(p);
    }
    if (peek(p) == '{') {
        return parse_container(p, FlJsonType_Object, false);
    }
    if (peek(p) == '[') {
        return parse_container(p, FlJsonType_Array, false);
    }
    if (at_number_start(p)) {
        return parse_number_value(p);
    }
    return parse_literal(p);
}

static FlJsonValue* parse_document(JsonParser* p) {
    if (p->json5) {
        skip_trivia(p);
        if (peek(p) != '{') {
            return parse_container(p, FlJsonType_Object, true);
        }
    }
    return parse_value(p);
}

static bool finish_document(JsonParser* p) {
    skip_trivia(p);
    return at_end(p) || fail(p, FlJsonParseError_UnexpectedTrailingCharacters);
}

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
    FlJsonValue* root = parse_document(&parser);
    if (root && !finish_document(&parser)) {
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
