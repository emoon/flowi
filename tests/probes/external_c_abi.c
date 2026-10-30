#include <flowi/arena/arena.h>
#include <flowi/core/log.h>
#include <flowi/string/string.h>
#include <flowi/ui/format.h>

#include "external_abi_probe.h"

#include <string.h>

FLOWI_TEST_EXPORT int external_c_variadic_abi_probe(void) {
    FlArena* arena = fl_arena_create(1024 * 1024, __FILE__, __LINE__);
    if (arena == NULL) {
        return 0;
    }

    FlString formatted = fl_sprintf_arena(arena, "c:%d", 17);
    FlStringBuilder builder = fl_sb_create(arena);
    builder = fl_sb_appendf(builder, "builder:%d", 23);
    FlString built = fl_sb_to_string(builder);
    fl_log_c_message(0, FlLogLevel_Info, __FILE__, __LINE__, "%s", "external-c");
    fl_log_message(FlLogLevel_Info, __FILE__, __LINE__, "%s", "external-c");

    int ok = formatted.length == 4 && memcmp(formatted.data, "c:17", 4) == 0 && built.length == 10
             && memcmp(built.data, "builder:23", 10) == 0;
    fl_arena_destroy(arena);
    return ok;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Must be called while libflowi has a frame open: fl_format_frame_string allocates from the frame arena of the UI
// context owned by the flowi instance running that frame, and this probe is linked against the shared library, not
// against the test executable's own statically linked copy.

FLOWI_TEST_EXPORT int external_c_frame_format_abi_probe(void) {
    FlString formatted = fl_format_frame_string("frame:%d", 31);
    return formatted.length == 8 && memcmp(formatted.data, "frame:31", 8) == 0;
}
