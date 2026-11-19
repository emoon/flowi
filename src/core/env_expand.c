#include "env_expand.h"
#include "arena.h"
#include "string.h"
#include "log.h"
#include <ctype.h>
#include <stdlib.h>

static inline bool is_var_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

typedef struct VarRef {
    FlString name;
    u64 next;
} VarRef;

static VarRef scan_var_ref(FlString input, u64 dollar_pos) {
    u64 start = dollar_pos + 1;
    bool has_braces = start < input.length && input.data[start] == '{';

    if (has_braces) {
        start++;
        i64 close = fl_string_find_char_from(input, '}', start);

        if (close == -1) {
            return (VarRef){ fl_string_substr(input, input.length, 0), input.length };
        }

        return (VarRef){ fl_string_substr(input, start, (u64)close - start), (u64)close + 1 };
    }

    u64 end = start;

    while (end < input.length && is_var_char(input.data[end])) {
        end++;
    }

    return (VarRef){ fl_string_substr(input, start, end - start), end };
}

static const char* lookup_env(FlArena* arena, FlString name) {
    if (name.length == 0) {
        return nullptr;
    }

    // The conflict-aware variant returns a scratch arena disjoint from the caller's
    // arena, which backs the string builder. Otherwise this scope's rewind would free
    // bytes sb appended inside it.
    arena_scratch_auto_conflict(temp, arena);
    return getenv(fl_string_to_cstr(temp.arena, name));
}

static FlStringBuilder append_range(FlStringBuilder sb, FlString input, u64 from, u64 to) {
    if (to <= from) {
        return sb;
    }

    return fl_sb_append_string(sb, fl_string_substr(input, from, to - from));
}

FlString expand_env_vars(FlArena* arena, FlString input) {
    if (fl_string_find_char(input, '$') == -1) {
        return input;
    }

    FlStringBuilder sb = fl_sb_create(arena);
    u64 pos = 0;

    while (pos < input.length) {
        i64 dollar_pos = fl_string_find_char_from(input, '$', pos);

        if (dollar_pos == -1) {
            return fl_sb_to_string(append_range(sb, input, pos, input.length));
        }

        sb = append_range(sb, input, pos, (u64)dollar_pos);

        VarRef ref = scan_var_ref(input, (u64)dollar_pos);
        const char* value = lookup_env(arena, ref.name);

        // Unset, empty or unterminated references keep their original text.
        sb = value != nullptr ? fl_sb_append_cstr(sb, value) : append_range(sb, input, (u64)dollar_pos, ref.next);
        pos = ref.next;
    }

    return fl_sb_to_string(sb);
}
