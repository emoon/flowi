#include "utest/utest.h"
#include <core/core.h>
#include <core/math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(base_math, count_trailing_zeros) {
    ASSERT_EQ(count_trailing_zeros(1), 0);
    ASSERT_EQ(count_trailing_zeros(2), 1);
    ASSERT_EQ(count_trailing_zeros(4), 2);
    ASSERT_EQ(count_trailing_zeros(8), 3);
    ASSERT_EQ(count_trailing_zeros(16), 4);
    ASSERT_EQ(count_trailing_zeros(32), 5);
    ASSERT_EQ(count_trailing_zeros(64), 6);
    ASSERT_EQ(count_trailing_zeros(128), 7);
    ASSERT_EQ(count_trailing_zeros(0x80000000), 31);
    ASSERT_EQ(count_trailing_zeros(12), 2); // 0b00001100
    ASSERT_EQ(count_trailing_zeros(0xFF00), 8);
}
