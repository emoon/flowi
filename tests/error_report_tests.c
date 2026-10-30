#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/error_report.h>
#include <core/log.h>
#include <core/os/os.h>
#include <core/string.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct error_report_fixture {
    LogChannelId test_channel;
};

UTEST_F_SETUP(error_report_fixture) {
    error_report_init();

    utest_fixture->test_channel = fl_log_register_channel(FL_S("TEST"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F_TEARDOWN(error_report_fixture) {
    error_report_cleanup();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, basic_error_formatting) {
    FlString error = error_message_format_no_log("Test error: %d", 42);

    ASSERT_TRUE(error.data != NULL);
    ASSERT_GT((u64)error.length, 0);
    ASSERT_TRUE(strstr(error.data, "Test error: 42") != NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, error_with_logging) {
    FlString error
        = error_message_format(utest_fixture->test_channel, LogLevel_Error, "File not found: %s", "test.txt");

    ASSERT_TRUE(error.data != NULL);
    ASSERT_GT((u64)error.length, 0);
    ASSERT_TRUE(strstr(error.data, "File not found: test.txt") != NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, error_without_logging) {
    FlString error = error_message_format(LOG_CHANNEL_INVALID, LogLevel_Error, "Silent error: %d", 123);

    ASSERT_TRUE(error.data != NULL);
    ASSERT_GT((u64)error.length, 0);
    ASSERT_TRUE(strstr(error.data, "Silent error: 123") != NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, cleanup_resets_arena) {
    FlString error1 = error_message_format_no_log("Error 1");
    FlString error2 = error_message_format_no_log("Error 2");
    FlString error3 = error_message_format_no_log("Error 3");

    ASSERT_TRUE(error1.data != NULL);
    ASSERT_TRUE(error2.data != NULL);
    ASSERT_TRUE(error3.data != NULL);

    error_report_cleanup();

    FlString error4 = error_message_format_no_log("Error 4");
    ASSERT_TRUE(error4.data != NULL);

    // error1/2/3 dangle after the cleanup above; only error4 is still valid here.
    ASSERT_GT((u64)error4.length, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, many_allocations) {
    const int num_allocations = 10000;

    for (int i = 0; i < num_allocations; i++) {
        FlString error = error_message_format_no_log("Error number %d with some extra text", i);
        ASSERT_TRUE(error.data != NULL);
        ASSERT_GT((u64)error.length, 0);

        // Periodically cleanup to avoid running out of arena space
        if (i % 1000 == 999) {
            error_report_cleanup();
        }
    }

    error_report_cleanup();

    FlString final_error = error_message_format_no_log("Final error");
    ASSERT_TRUE(final_error.data != NULL);
    ASSERT_GT((u64)final_error.length, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ThreadTestData {
    LogChannelId channel;
    int thread_id;
    int num_iterations;
    _Atomic int success_count;
} ThreadTestData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* thread_test_worker(void* arg) {
    ThreadTestData* data = (ThreadTestData*)arg;

    for (int i = 0; i < data->num_iterations; i++) {
        FlString error = error_message_format_no_log("Thread %d iteration %d", data->thread_id, i);

        if (error.data != NULL && error.length > 0) {
            atomic_fetch_add(&data->success_count, 1);
        }
    }

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, thread_safety) {
#define NUM_TEST_THREADS 4
#define ITERATIONS_PER_THREAD 1000

    Thread threads[NUM_TEST_THREADS];
    ThreadTestData thread_data[NUM_TEST_THREADS];

    for (int i = 0; i < NUM_TEST_THREADS; i++) {
        thread_data[i].channel = utest_fixture->test_channel;
        thread_data[i].thread_id = i;
        thread_data[i].num_iterations = ITERATIONS_PER_THREAD;
        atomic_store(&thread_data[i].success_count, 0);

        bool result = os_thread_create(&threads[i], thread_test_worker, &thread_data[i]);
        ASSERT_TRUE(result);
    }

    for (int i = 0; i < NUM_TEST_THREADS; i++) {
        os_thread_join(&threads[i]);
    }

    int total_success = 0;
    for (int i = 0; i < NUM_TEST_THREADS; i++) {
        total_success += atomic_load(&thread_data[i].success_count);
    }

    ASSERT_EQ(total_success, NUM_TEST_THREADS * ITERATIONS_PER_THREAD);
#undef NUM_TEST_THREADS
#undef ITERATIONS_PER_THREAD
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, convenience_macros) {
    FlString error = error_message_error(utest_fixture->test_channel, "Error message");
    ASSERT_TRUE(error.data != NULL);
    ASSERT_TRUE(strstr(error.data, "Error message") != NULL);

    FlString warning = error_message_warning(utest_fixture->test_channel, "Warning message");
    ASSERT_TRUE(warning.data != NULL);
    ASSERT_TRUE(strstr(warning.data, "Warning message") != NULL);

    FlString info = error_message_info(utest_fixture->test_channel, "Info message");
    ASSERT_TRUE(info.data != NULL);
    ASSERT_TRUE(strstr(info.data, "Info message") != NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, custom_format_specifiers) {
    FlString path = FL_S("/path/to/file.txt");
    FlString name = FL_S("example");

    FlString error = error_message_format_no_log("Failed to load %S from %S", name, path);

    ASSERT_TRUE(error.data != NULL);
    ASSERT_GT((u64)error.length, 0);
    ASSERT_TRUE(strstr(error.data, "Failed to load example from /path/to/file.txt") != NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, multiple_rpstring_format) {
    FlString str1 = FL_S("first");
    FlString str2 = FL_S("second");
    FlString str3 = FL_S("third");

    FlString error = error_message_format_no_log("Strings: %S, %S, %S", str1, str2, str3);

    ASSERT_TRUE(error.data != NULL);
    ASSERT_TRUE(strstr(error.data, "first") != NULL);
    ASSERT_TRUE(strstr(error.data, "second") != NULL);
    ASSERT_TRUE(strstr(error.data, "third") != NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, mixed_format_specifiers) {
    FlString filename = FL_S("config.json");
    int line = 42;
    const char* reason = "syntax error";

    FlString error = error_message_format_no_log("Parse error in %S at line %d: %s", filename, line, reason);

    ASSERT_TRUE(error.data != NULL);
    ASSERT_TRUE(strstr(error.data, "config.json") != NULL);
    ASSERT_TRUE(strstr(error.data, "42") != NULL);
    ASSERT_TRUE(strstr(error.data, "syntax error") != NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, empty_format_string) {
    FlString error = error_message_format_no_log("");

    ASSERT_TRUE(error.data != NULL);
    ASSERT_EQ((u64)error.length, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, long_error_message) {
    const char* long_text = "This is a very long error message that contains a lot of text to ensure that the "
                            "arena allocation can handle larger strings without issues. We want to make sure "
                            "that the thread-safe arena properly allocates space for messages of varying sizes.";

    FlString error = error_message_format_no_log("%s", long_text);

    ASSERT_TRUE(error.data != NULL);
    ASSERT_EQ((size_t)error.length, strlen(long_text));
    ASSERT_EQ(memcmp(error.data, long_text, error.length), 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString test_vformat_wrapper(LogChannelId channel, LogLevel level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FlString result = error_message_vformat(channel, level, fmt, args);
    va_end(args);
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString test_vformat_no_log_wrapper(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FlString result = error_message_vformat_no_log(fmt, args);
    va_end(args);
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, vformat_variants) {
    FlString error1 = test_vformat_wrapper(utest_fixture->test_channel, LogLevel_Error, "Error: %d", 123);
    ASSERT_TRUE(error1.data != NULL);
    ASSERT_TRUE(strstr(error1.data, "Error: 123") != NULL);

    FlString error2 = test_vformat_no_log_wrapper("Message: %s", "test");
    ASSERT_TRUE(error2.data != NULL);
    ASSERT_TRUE(strstr(error2.data, "Message: test") != NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(error_report_fixture, consecutive_allocations) {
#define TEST_ERROR_COUNT 100
    FlString errors[TEST_ERROR_COUNT];

    for (int i = 0; i < TEST_ERROR_COUNT; i++) {
        errors[i] = error_message_format_no_log("Error %d", i);
        ASSERT_TRUE(errors[i].data != NULL);
        ASSERT_GT((u64)errors[i].length, 0);
    }

    for (int i = 0; i < TEST_ERROR_COUNT; i++) {
        char expected[32];
        snprintf(expected, sizeof(expected), "Error %d", i);
        ASSERT_TRUE(strstr(errors[i].data, expected) != NULL);
    }
#undef TEST_ERROR_COUNT
}
