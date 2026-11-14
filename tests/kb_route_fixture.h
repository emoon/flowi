#pragma once

#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/os/os.h>
#include <core/string.h>
#include <ui/font_cache.h>

typedef struct KbRoute {
    FlArena* arena;
    FlFontCache* cache;
    FileMmap roboto_file;
    FlFontId roboto;
} KbRoute;

static inline KbRoute kb_route_open(void) {
    KbRoute route = { .arena = arena_new() };
    route.cache = font_cache_new(route.arena);
    route.roboto_file = file_mmap_open_os(FL_S(FLOWI_TEST_DATA_DIR "/Roboto-Regular.ttf"));
    route.roboto = font_cache_load_font_memory(route.cache, route.roboto_file.data, (i64)route.roboto_file.size, 16);
    return route;
}

static inline void kb_route_close(KbRoute* route) {
    font_cache_destroy(route->cache);
    fl_arena_destroy(route->arena);
    file_mmap_close_os(&route->roboto_file);
}
