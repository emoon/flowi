#include "env_expand.h"
#include "arena.h"
#include "string.h"
#include "log.h"
#include <ctype.h>
#include <stdlib.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline bool is_var_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Supports ${VAR} and $VAR syntax

FlString expand_env_vars(FlArena* arena, FlString input) {
    if (fl_string_find_char(input, '$') == -1) {
        return input;
    }

    FlStringBuilder sb = fl_sb_create(arena);
    u64 pos = 0;

    while (pos < input.length) {
        i64 dollar_pos = fl_string_find_char_from(input, '$', pos);

        if (dollar_pos == -1) {
            sb = fl_sb_append_string(sb, fl_string_substr(input, pos, input.length - pos));
            break;
        }

        if (dollar_pos > (i64)pos) {
            sb = fl_sb_append_string(sb, fl_string_substr(input, pos, dollar_pos - pos));
        }

        u64 var_start = (u64)dollar_pos + 1;

        // Trailing '$'
        if (var_start >= input.length) {
            sb = fl_sb_append_char(sb, '$');
            break;
        }

        bool has_braces = false;

        if (input.data[var_start] == '{') {
            has_braces = true;
            var_start++;
        }

        u64 var_end = var_start;

        if (has_braces) {
            while (var_end < input.length && input.data[var_end] != '}') {
                var_end++;
            }

            // No closing brace - keep original text
            if (var_end >= input.length) {
                sb = fl_sb_append_string(sb, fl_string_substr(input, (u64)dollar_pos, input.length - dollar_pos));
                break;
            }
        } else {
            while (var_end < input.length && is_var_char(input.data[var_end])) {
                var_end++;
            }
        }

        FlString var_name = fl_string_substr(input, var_start, var_end - var_start);

        if (var_name.length > 0) {
            // The conflict-aware variant guarantees a scratch arena disjoint from the caller's
            // arena, which backs the string builder - otherwise this scope's rewind would free
            // bytes sb appended inside it.
            arena_scratch_auto_conflict(temp, arena);
            const char* var_name_cstr = fl_string_to_cstr(temp.arena, var_name);
            const char* var_value = getenv(var_name_cstr);

            if (var_value != nullptr) {
                sb = fl_sb_append_cstr(sb, var_value);
            } else {
                // Variable not found - keep original ${VAR} or $VAR in output
                if (has_braces) {
                    sb = fl_sb_append_string(sb, fl_string_substr(input, (u64)dollar_pos, var_end - dollar_pos + 1));
                } else {
                    sb = fl_sb_append_string(sb, fl_string_substr(input, (u64)dollar_pos, var_end - dollar_pos));
                }
            }
        } else {
            // Empty variable name (like ${} or $) - keep original
            if (has_braces) {
                sb = fl_sb_append_cstr(sb, "${}");
            } else {
                sb = fl_sb_append_char(sb, '$');
            }
        }

        pos = var_end + (has_braces ? 1 : 0);
    }

    return fl_sb_to_string(sb);
}
