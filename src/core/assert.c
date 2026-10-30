#include "assert.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_assert_fatal_handler(const char* expr_str, const char* file, int line) {
    log_fatal("Assertion failed: %s at %s:%d", expr_str, file, line);
    fl_log_flush();

#ifdef NDEBUG
    exit(1);
#else
    abort();
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_assert_handler(const char* expr_str, const char* file, int line, bool fatal) {
    if (fatal) {
        fl_assert_fatal_handler(expr_str, file, line);
    }

    log_error("Assertion failed: %s at %s:%d", expr_str, file, line);
    return false;
}
