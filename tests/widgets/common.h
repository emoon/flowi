#pragma once

// Shared helpers for the widget unit_tests_*.c translation units.

#include "unit_tests_common.h"

// Ends the calling UTEST once a helper's ASSERT_* has failed, since an ASSERT_* inside a helper only leaves the helper.
#define RETURN_IF_TEST_FAILED()                   \
    do {                                          \
        if (*utest_result != UTEST_TEST_PASSED) { \
            return;                               \
        }                                         \
    } while (0)

// One tolerance comparison with its failure message, so a run of related measurements reads as a table.
typedef struct NearCheck {
    double actual;
    double expected;
    double tolerance;
    const char* message;
} NearCheck;

#define NEAR_CHECK_TOL(actual, expected, tolerance, message) \
    { (double)(actual), (double)(expected), (tolerance), (message) }

#define NEAR_CHECK(actual, expected, message) NEAR_CHECK_TOL(actual, expected, 1e-4, message)

static inline void assert_near_checks(int* utest_result, const NearCheck* checks, size_t count) {
    for (size_t i = 0; i < count; i++) {
        ASSERT_NEAR_MSG(checks[i].actual, checks[i].expected, checks[i].tolerance, checks[i].message);
    }
}

static inline void expect_near_checks(int* utest_result, const NearCheck* checks, size_t count) {
    for (size_t i = 0; i < count; i++) {
        EXPECT_NEAR_MSG(checks[i].actual, checks[i].expected, checks[i].tolerance, checks[i].message);
    }
}

// One boolean condition with its failure message.
typedef struct TrueCheck {
    bool condition;
    const char* message;
} TrueCheck;

#define TRUE_CHECK(condition, message) { (condition), (message) }

static inline void assert_true_checks(int* utest_result, const TrueCheck* checks, size_t count) {
    for (size_t i = 0; i < count; i++) {
        ASSERT_TRUE_MSG(checks[i].condition, checks[i].message);
    }
}

// Asserts every TRUE_CHECK listed and ends the calling UTEST at the first one that fails.
#define ASSERT_TRUE_CHECKS(...)                                                                         \
    do {                                                                                                \
        const TrueCheck true_checks_[] = { __VA_ARGS__ };                                               \
        assert_true_checks(utest_result, true_checks_, sizeof(true_checks_) / sizeof(true_checks_[0])); \
        RETURN_IF_TEST_FAILED();                                                                        \
    } while (0)

// Asserts every NEAR_CHECK listed and ends the calling UTEST at the first one that fails.
#define ASSERT_NEAR_CHECKS(...)                                                                         \
    do {                                                                                                \
        const NearCheck near_checks_[] = { __VA_ARGS__ };                                               \
        assert_near_checks(utest_result, near_checks_, sizeof(near_checks_) / sizeof(near_checks_[0])); \
        RETURN_IF_TEST_FAILED();                                                                        \
    } while (0)

// Expects every NEAR_CHECK listed and lets the calling UTEST continue past a failure.
#define EXPECT_NEAR_CHECKS(...)                                                                         \
    do {                                                                                                \
        const NearCheck near_checks_[] = { __VA_ARGS__ };                                               \
        expect_near_checks(utest_result, near_checks_, sizeof(near_checks_) / sizeof(near_checks_[0])); \
    } while (0)

// One frame of a synthetic pointer gesture: the cursor position and the left-button state, which the real input layer
// then consumes. Because the frame goes through ui_input_update, signal.clicked and signal.hovered come from the real
// hit testing.
typedef struct PointerFrame {
    float x;
    float y;
    bool held;
    bool pressed;
    bool released;
} PointerFrame;

static inline void pointer_frame_apply(FlUiCtx* ctx, PointerFrame pointer) {
    FlInputState* in = &ctx->input_state;
    ui_input_set_mouse_position(ctx, pointer.x, pointer.y);
    in->mouse.buttons[FlMouseButton_Left] = pointer.held;
    in->mouse.buttons_pressed[FlMouseButton_Left] = pointer.pressed; // ui_input_update consumes and clears this edge
    in->mouse.buttons_released[FlMouseButton_Left] = pointer.released;
    ui_input_update(ctx, 1.0f / 60.0f);
}

// One string comparison with its failure message.
typedef struct StringCheck {
    FlString got;
    FlString want;
    const char* message;
} StringCheck;

#define STRING_CHECK(got, want, message) { (got), (want), (message) }

static inline void assert_string_checks(int* utest_result, const StringCheck* checks, size_t count) {
    for (size_t i = 0; i < count; i++) {
        ASSERT_TRUE_MSG(fl_string_equals(checks[i].got, checks[i].want), checks[i].message);
    }
}

// Asserts every STRING_CHECK listed and ends the calling UTEST at the first one that fails.
#define ASSERT_STRING_CHECKS(...)                                                                               \
    do {                                                                                                        \
        const StringCheck string_checks_[] = { __VA_ARGS__ };                                                   \
        assert_string_checks(utest_result, string_checks_, sizeof(string_checks_) / sizeof(string_checks_[0])); \
        RETURN_IF_TEST_FAILED();                                                                                \
    } while (0)
