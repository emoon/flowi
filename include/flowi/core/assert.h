#pragma once

// Assertion failures route through fl_assert_handler, which logs the expression with file:line.
// Debug builds abort; release builds log and let the caller recover.

#include <flowi/core/platform.h>
#include <stdbool.h>

// Provides static_assert
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called when an assertion fails. Logs the failure and exits when fatal, otherwise returns false so the
// caller can handle the failure.

bool fl_assert_handler(const char* expr_str, const char* file, int line, bool fatal);

// The always-fatal variant. NORETURN tells the compiler and static analysers that the path ends at a failed
// assert.

NORETURN void fl_assert_fatal_handler(const char* expr_str, const char* file, int line);

// Checked in both debug and release builds. Debug aborts; release logs and continues.
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

// Always exits on failure, in debug and release.
#define FL_ASSERT_FATAL(expr)                                   \
    do {                                                        \
        if (FL_UNLIKELY(!(expr))) {                                \
            fl_assert_fatal_handler(#expr, __FILE__, __LINE__); \
        }                                                       \
    } while (0)

// FL_ASSERT_FATAL with a custom message.
#define FL_ASSERT_FATAL_MSG(expr, msg)                                    \
    do {                                                                  \
        if (FL_UNLIKELY(!(expr))) {                                          \
            fl_assert_fatal_handler(#expr " - " msg, __FILE__, __LINE__); \
        }                                                                 \
    } while (0)

// Compiles out in release builds.
#ifdef NDEBUG
#define FL_ASSERT_DEBUG(expr) ((void)0)
#else
#define FL_ASSERT_DEBUG(expr) FL_ASSERT(expr)
#endif

// FL_ASSERT with a custom message.
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

// Validation macros: assertion plus early return.

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

// FL_VALIDATE_RET that also runs log_call before returning.
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

#ifdef __cplusplus
}
#endif
