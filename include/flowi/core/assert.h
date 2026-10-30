#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Flowi Assert System
//
// All failures route through fl_assert_handler, which logs the expression with file:line.
// Debug builds abort; release builds log and let the caller recover.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <flowi/core/platform.h>
#include <stdbool.h>

// Provides static_assert
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Assert Handler
//
// Called when an assertion fails. Logs the failure and exits when fatal, otherwise returns false so the
// caller can handle the failure.

bool fl_assert_handler(const char* expr_str, const char* file, int line, bool fatal);

// The always-fatal half, split out so the compiler knows the call never comes back: without that a static
// analyser walks straight past a failed assert and reports the impossible state the assert exists to rule out.

NORETURN void fl_assert_fatal_handler(const char* expr_str, const char* file, int line);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Assertion Macros

// Main assertion macro - checks in both debug and release builds
#ifdef NDEBUG
#define FL_ASSERT(expr)                                          \
    do {                                                         \
        if (FL_UNLIKELY(!(expr))) {                                 \
            fl_assert_handler(#expr, __FILE__, __LINE__, false); \
        }                                                        \
    } while (0)
#else
#define FL_ASSERT(expr)                                         \
    do {                                                        \
        if (FL_UNLIKELY(!(expr))) {                                \
            fl_assert_fatal_handler(#expr, __FILE__, __LINE__); \
        }                                                       \
    } while (0)
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Fatal assertion - always exits on failure (debug and release)
#define FL_ASSERT_FATAL(expr)                                   \
    do {                                                        \
        if (FL_UNLIKELY(!(expr))) {                                \
            fl_assert_fatal_handler(#expr, __FILE__, __LINE__); \
        }                                                       \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Fatal assertion with a custom message - always exits on failure (debug and release)
#define FL_ASSERT_FATAL_MSG(expr, msg)                                    \
    do {                                                                  \
        if (FL_UNLIKELY(!(expr))) {                                          \
            fl_assert_fatal_handler(#expr " - " msg, __FILE__, __LINE__); \
        }                                                                 \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Debug-only assertion - compiles out in release builds
#ifdef NDEBUG
#define FL_ASSERT_DEBUG(expr) ((void)0)
#else
#define FL_ASSERT_DEBUG(expr) FL_ASSERT(expr)
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Assertion with custom message
#ifdef NDEBUG
#define FL_ASSERT_MSG(expr, msg)                                           \
    do {                                                                   \
        if (FL_UNLIKELY(!(expr))) {                                           \
            fl_assert_handler(#expr " - " msg, __FILE__, __LINE__, false); \
        }                                                                  \
    } while (0)
#else
#define FL_ASSERT_MSG(expr, msg)                                          \
    do {                                                                  \
        if (FL_UNLIKELY(!(expr))) {                                          \
            fl_assert_fatal_handler(#expr " - " msg, __FILE__, __LINE__); \
        }                                                                 \
    } while (0)
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validation Macros
//
// Assertion plus early return. The assertion logic is inlined to preserve correct file/line info.

#ifdef NDEBUG
#define FL_VALIDATE(expr)                                        \
    do {                                                         \
        if (FL_UNLIKELY(!(expr))) {                                 \
            fl_assert_handler(#expr, __FILE__, __LINE__, false); \
            return;                                              \
        }                                                        \
    } while (0)
#else
#define FL_VALIDATE(expr)                                       \
    do {                                                        \
        if (FL_UNLIKELY(!(expr))) {                                \
            fl_assert_fatal_handler(#expr, __FILE__, __LINE__); \
            return;                                             \
        }                                                       \
    } while (0)
#endif

#ifdef NDEBUG
#define FL_VALIDATE_RET(expr, return_value)                      \
    do {                                                         \
        if (FL_UNLIKELY(!(expr))) {                                 \
            fl_assert_handler(#expr, __FILE__, __LINE__, false); \
            return return_value;                                 \
        }                                                        \
    } while (0)
#else
#define FL_VALIDATE_RET(expr, return_value)                     \
    do {                                                        \
        if (FL_UNLIKELY(!(expr))) {                                \
            fl_assert_fatal_handler(#expr, __FILE__, __LINE__); \
            return return_value;                                \
        }                                                       \
    } while (0)
#endif

// Validation with custom error logging
#ifdef NDEBUG
#define FL_VALIDATE_LOG(expr, return_value, log_call)            \
    do {                                                         \
        if (FL_UNLIKELY(!(expr))) {                                 \
            fl_assert_handler(#expr, __FILE__, __LINE__, false); \
            log_call;                                            \
            return return_value;                                 \
        }                                                        \
    } while (0)
#else
#define FL_VALIDATE_LOG(expr, return_value, log_call)           \
    do {                                                        \
        if (FL_UNLIKELY(!(expr))) {                                \
            fl_assert_fatal_handler(#expr, __FILE__, __LINE__); \
            log_call;                                           \
            return return_value;                                \
        }                                                       \
    } while (0)
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
