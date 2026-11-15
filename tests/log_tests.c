#include "utest/utest.h"
#include <core/core.h>
#include <core/log.h>
#include <core/string.h>

UTEST(log_channels, existing_name_is_found_when_the_table_is_full) {
    static char names[64][8];
    const LogChannelId existing = fl_log_register_channel(FL_S("LOG_TEST_EXISTING"));
    ASSERT_NE(LOG_CHANNEL_INVALID, existing);

    for_count(i, 64) {
        names[i][0] = 'f';
        names[i][1] = (char)('a' + i / 26);
        names[i][2] = (char)('a' + i % 26);
        const FlString name = { .data = names[i], .length = 3 };
        if (fl_log_register_channel(name) == LOG_CHANNEL_INVALID) {
            break;
        }
    }

    ASSERT_EQ(LOG_CHANNEL_INVALID, fl_log_register_channel(FL_S("LOG_TEST_NEW")));
    ASSERT_EQ(existing, fl_log_register_channel(FL_S("LOG_TEST_EXISTING")));
}
