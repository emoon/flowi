#include "localization_private.h"

// Placeholder substitution: fills a localized string's {index} slots from format arguments.

// Whether kind names an arm of the union. An argument that fails this carries nothing readable, so the slot it
// would have filled is left as written, exactly like a slot with no argument at all.

static bool arg_is_readable(FlFmtArg arg) {
    return arg.kind == FlFmtArgKind_Int || arg.kind == FlFmtArgKind_Float || arg.kind == FlFmtArgKind_Str;
}

static FlStringBuilder append_arg(FlStringBuilder sb, FlFmtArg arg) {
    switch (arg.kind) {
        case FlFmtArgKind_Int:
            return fl_sb_appendf(sb, "%lld", (long long)arg.value.int_value);
        case FlFmtArgKind_Float:
            return fl_sb_appendf(sb, "%g", arg.value.float_value);
        case FlFmtArgKind_Str:
            return fl_sb_append_string(sb, arg.value.string_value);
    }

    return sb;
}

// The readable argument a slot body names, or null when the body is not an index, points past the end of args, or
// names an argument with nothing readable in it.

static const FlFmtArg* slot_argument(FlString body, FmtArgs args) {
    u32 index = 0;
    if (!localization_slot_index(body, &index) || index >= args.count) {
        return nullptr;
    }
    return arg_is_readable(args.items[index]) ? &args.items[index] : nullptr;
}

FlString localization_substitute(FlArena* arena, FlString text, FmtArgs args, bool* out_unmatched) {
    *out_unmatched = false;
    FlStringBuilder sb = fl_sb_create(arena);
    LocalizationToken token = { 0 };
    u64 cursor = 0;

    while (localization_next_token(text, &cursor, &token)) {
        const FlFmtArg* arg = token.is_slot ? slot_argument(token.body, args) : nullptr;
        if (arg != nullptr) {
            sb = append_arg(sb, *arg);
            continue;
        }

        if (token.is_slot) {
            *out_unmatched = true;
        }
        sb = fl_sb_append_string(sb, token.text);
    }

    return fl_sb_to_string(sb);
}
