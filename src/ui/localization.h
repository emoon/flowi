#pragma once

#include <core/string.h>
#include <core/types.h>
#include <string.h>

struct FlVfsMount;
struct Localization;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start loading the manifest at manifest_path inside mount; localization_frame_begin finishes it and brings up
// the default language's table. Never returns nullptr: a load that cannot start, or a manifest that names no
// usable default, leaves localization disabled, and every string then resolves to itself.

struct Localization* localization_create(struct FlVfsMount* mount, FlString manifest_path, bool hot_reload);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void localization_destroy(struct Localization* self);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame-boundary hook, and the only place localization state changes. Every read - the manifest, the default
// table, a language recorded by fl_localization_set_language - is parsed and built on a VFS worker; this polls
// whichever one is in flight and swaps pointers on the boundary it lands, so no call here ever waits on I/O. Until
// the default table lands, every SID resolves to itself; a switch keeps the current language until its table is
// ready. No-op once nothing is in flight.

void localization_frame_begin(struct Localization* self);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Whether any read is still in flight: the manifest, the default table, or a Language Switch. Text resolves
// against whatever is already loaded meanwhile, so this is not a gate callers must wait on.

bool localization_is_loading(struct Localization* self);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// How many string tables are resident: one while the active language is the default, two otherwise, zero before
// any table has loaded.

u32 localization_resident_table_count(struct Localization* self);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// How many reloads the file watcher has asked for since creation (a changed manifest, active table or default
// table), and whether a watch is live at all.

u64 localization_hot_reload_request_count(struct Localization* self);
bool localization_is_watching(struct Localization* self);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Look up a SID: active table, then the default table, then the SID itself when neither carries the key.

FlString localization_lookup(struct Localization* self, FlString sid);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The CLDR plural categories, in the order a table entry's variants are indexed. A Plural Variant object names a
// subset of them; only other is required, and it is what every language selects for a count no other category
// claims.

typedef enum LocalizationPluralCategory {
    LocalizationPlural_Zero,
    LocalizationPlural_One,
    LocalizationPlural_Two,
    LocalizationPlural_Few,
    LocalizationPlural_Many,
    LocalizationPlural_Other,
    LocalizationPlural_Count,
} LocalizationPluralCategory;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Which category count selects in the language tag names. Rules are built in and keyed by the tag's primary
// subtag, so pt-br selects Portuguese's; a language with no built-in rules selects other for every count.
// Negative counts select as their magnitude does.

LocalizationPluralCategory localization_plural_category(FlString tag, i64 count);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Whether text carries the SID_ prefix that marks it as a localization key.

static inline bool localization_is_sid(FlString text) {
    return text.length >= 4 && memcmp(text.data, "SID_", 4) == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// One step of the placeholder grammar. text is the source bytes the token spans - what a caller with nothing to
// substitute copies through as written, already collapsed to the single brace an escaped pair stands for. body
// is the slot body between the braces, and is meaningful only while is_slot.

typedef struct LocalizationToken {
    FlString text;
    FlString body;
    bool is_slot;
} LocalizationToken;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Walk text one token at a time from `*cursor`, returning false at the end. {{ and }} are escaped braces, a
// } that closes nothing and a { that nothing closes are literal text, and everything from a { to its } is
// one slot.

static inline bool localization_next_token(FlString text, u64* cursor, LocalizationToken* out) {
    if (*cursor >= text.length) {
        return false;
    }

    u64 i = *cursor;
    while (i < text.length && text.data[i] != '{' && text.data[i] != '}') {
        i++;
    }
    if (i > *cursor) {
        *out = (LocalizationToken) { .text = fl_string_substr(text, *cursor, i - *cursor) };
        *cursor = i;
        return true;
    }

    const char brace = text.data[i];
    if (i + 1 < text.length && text.data[i + 1] == brace) {
        *out = (LocalizationToken) { .text = fl_string_substr(text, i, 1) };
        *cursor = i + 2;
        return true;
    }

    const i64 close = brace == '{' ? fl_string_find_char_from(text, '}', i + 1) : -1;
    if (close < 0) {
        // An unterminated slot swallows the rest of the text; a stray closing brace is just itself.
        const u64 length = brace == '{' ? text.length - i : 1;
        *out = (LocalizationToken) { .text = fl_string_substr(text, i, length) };
        *cursor = i + length;
        return true;
    }

    *out = (LocalizationToken) { .text = fl_string_substr(text, i, (u64)close + 1 - i),
                                 .body = fl_string_substr(text, i + 1, (u64)close - i - 1),
                                 .is_slot = true };
    *cursor = (u64)close + 1;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The argument index a placeholder slot body names. A body is an index only when every byte is a digit; anything
// else is a named slot, which no argument list can match.

static inline bool localization_slot_index(FlString body, u32* out_index) {
    if (body.length == 0 || body.length > 9) {
        return false;
    }

    u32 index = 0;
    for (u64 i = 0; i < body.length; i++) {
        const char c = body.data[i];
        if (c < '0' || c > '9') {
            return false;
        }
        index = index * 10 + (u32)(c - '0');
    }

    *out_index = index;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resolve display text: a SID becomes its translation, anything else passes through untouched.

static inline FlString localization_resolve(struct Localization* self, FlString text) {
    if (self == nullptr || !localization_is_sid(text)) {
        return text;
    }
    return localization_lookup(self, text);
}
