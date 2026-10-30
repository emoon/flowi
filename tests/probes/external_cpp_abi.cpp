#include <flowi/arena/arena.h>
#include <flowi/string/string.h>
#include <flowi/ui/format.h>

#include "external_abi_probe.h"

#include <cstring>

extern "C" FLOWI_TEST_EXPORT int external_cpp_abi_probe(void) {
    FlArena* arena = fl_arena_create(1024 * 1024, __FILE__, __LINE__);
    if (arena == nullptr) {
        return 0;
    }

    FlString formatted = fl_sprintf_arena(arena, "cpp:%d", 47);
    int ok = formatted.length == 6 && std::memcmp(formatted.data, "cpp:47", 6) == 0;
    fl_arena_destroy(arena);
    return ok;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// As in external_c_abi.c: only valid while libflowi has a frame open, since the string comes from that instance's
// frame arena.

extern "C" FLOWI_TEST_EXPORT int external_cpp_frame_format_abi_probe(void) {
    FlString formatted = fl_format_frame_string("cpp-frame:%d", 59);
    return formatted.length == 12 && std::memcmp(formatted.data, "cpp-frame:59", 12) == 0;
}
