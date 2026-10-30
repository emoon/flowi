#pragma once

// C call-site sugar over fl_localize_fmt / fl_localize_plural: FL_FMT and FL_PLURAL tag bare
// arguments by type, build the FlFmtArg array on the stack, and expand to the real call, so a
// localized message carrying runtime values passes straight into a widget.
//
//     ui_text(FL_FMT("SID_greeting", name));
//     ui_text(FL_PLURAL("SID_file_count", file_count));

#include <flowi/core/string.h>
#include <flowi/ui/localization.h>

// Argument constructors. FL_ARG selects one of these per bare argument; the FL_ARG_* macros below
// call them directly for a value FL_ARG does not dispatch.

static inline FlFmtArg fl_fmt_arg_int(int64_t value) {
    return (FlFmtArg) { .value.int_value = value, .kind = FlFmtArgKind_Int };
}

static inline FlFmtArg fl_fmt_arg_float(double value) {
    return (FlFmtArg) { .value.float_value = value, .kind = FlFmtArgKind_Float };
}

static inline FlFmtArg fl_fmt_arg_string(FlString value) {
    return (FlFmtArg) { .value.string_value = value, .kind = FlFmtArgKind_Str };
}

static inline FlFmtArg fl_fmt_arg_cstr(const char* value) {
    return fl_fmt_arg_string(string_from_cstr(value));
}

static inline FlFmtArg fl_fmt_arg_pass(FlFmtArg value) {
    return value;
}

// Tag a value explicitly. Mix these freely with bare arguments inside FL_FMT / FL_PLURAL.
#define FL_ARG_INT(value) fl_fmt_arg_int((int64_t)(value))
#define FL_ARG_FLOAT(value) fl_fmt_arg_float((double)(value))
#define FL_ARG_STR(value) fl_fmt_arg_string(value)

// Tag one bare argument from its type. A type not listed here fails to compile rather than tagging
// as something it is not; wrap such a value in one of the FL_ARG_* macros.
#define FL_ARG(value)                   \
    _Generic((value),                   \
        FlFmtArg: fl_fmt_arg_pass,      \
        FlString: fl_fmt_arg_string,    \
        char*: fl_fmt_arg_cstr,         \
        const char*: fl_fmt_arg_cstr,   \
        float: fl_fmt_arg_float,        \
        double: fl_fmt_arg_float,       \
        _Bool: fl_fmt_arg_int,          \
        char: fl_fmt_arg_int,           \
        signed char: fl_fmt_arg_int,    \
        unsigned char: fl_fmt_arg_int,  \
        short: fl_fmt_arg_int,          \
        unsigned short: fl_fmt_arg_int, \
        int: fl_fmt_arg_int,            \
        unsigned int: fl_fmt_arg_int,   \
        long: fl_fmt_arg_int,           \
        unsigned long: fl_fmt_arg_int,  \
        long long: fl_fmt_arg_int,      \
        unsigned long long: fl_fmt_arg_int)(value)

// Arity dispatch. FL_FMT_PICK returns its eleventh token, so the trailing name list lines the
// argument count up with the expansion for that count and pushes anything past eight onto an
// undeclared identifier.
#define FL_FMT_PICK(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, name, ...) name

#define FL_FMT_ARGS_0() fl_fmt_arg_int(0)
#define FL_FMT_ARGS_1(a) FL_ARG(a)
#define FL_FMT_ARGS_2(a, ...) FL_ARG(a), FL_FMT_ARGS_1(__VA_ARGS__)
#define FL_FMT_ARGS_3(a, ...) FL_ARG(a), FL_FMT_ARGS_2(__VA_ARGS__)
#define FL_FMT_ARGS_4(a, ...) FL_ARG(a), FL_FMT_ARGS_3(__VA_ARGS__)
#define FL_FMT_ARGS_5(a, ...) FL_ARG(a), FL_FMT_ARGS_4(__VA_ARGS__)
#define FL_FMT_ARGS_6(a, ...) FL_ARG(a), FL_FMT_ARGS_5(__VA_ARGS__)
#define FL_FMT_ARGS_7(a, ...) FL_ARG(a), FL_FMT_ARGS_6(__VA_ARGS__)
#define FL_FMT_ARGS_8(a, ...) FL_ARG(a), FL_FMT_ARGS_7(__VA_ARGS__)
#define FL_FMT_ARGS_OVER_8(...) FL_FMT_TAKES_AT_MOST_8_ARGUMENTS

// lead is a placeholder token that gives the variadic list a named parameter to follow, which is
// what lets `, ##__VA_ARGS__` drop the comma for an empty list. The zero-argument array still holds
// one element because C11 has no empty initializer; the count stays 0, so nothing reads it.
#define FL_FMT_ARG_ARRAY(lead, ...)                                                                              \
    ((const FlFmtArg[]) { FL_FMT_PICK(lead, ##__VA_ARGS__, FL_FMT_ARGS_OVER_8, FL_FMT_ARGS_8, FL_FMT_ARGS_7,     \
                                      FL_FMT_ARGS_6, FL_FMT_ARGS_5, FL_FMT_ARGS_4, FL_FMT_ARGS_3, FL_FMT_ARGS_2, \
                                      FL_FMT_ARGS_1, FL_FMT_ARGS_0, 0)(__VA_ARGS__) })

#define FL_FMT_ARG_COUNT(lead, ...) \
    FL_FMT_PICK(lead, ##__VA_ARGS__, FL_FMT_TAKES_AT_MOST_8_ARGUMENTS, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0)

// Look the string-literal key sid up in the active language and substitute the arguments that
// follow into its indexed placeholders - {0} is the first, {1} the second. At most eight
// arguments. The result is frame-lifetime, the same as fl_localize_fmt's.
#define FL_FMT(sid, ...) fl_localize_fmt(FL_S(sid), FL_FMT_ARG_ARRAY(0, ##__VA_ARGS__), FL_FMT_ARG_COUNT(0, ##__VA_ARGS__))

// Look the string-literal key sid up, pick the plural variant count selects, and substitute.
// count is itself {0} and the arguments that follow are {1} onwards. At most eight arguments
// after the count.
#define FL_PLURAL(sid, count, ...) \
    fl_localize_plural(FL_S(sid), (int64_t)(count), FL_FMT_ARG_ARRAY(0, ##__VA_ARGS__), FL_FMT_ARG_COUNT(0, ##__VA_ARGS__))
