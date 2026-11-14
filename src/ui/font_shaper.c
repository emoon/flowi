#include "font_shaper.h"
#include <core/arena.h>
#include <core/heap.h>
#include <kb/kb_text_shape.h>
#include <limits.h>
#include <stddef.h>

struct FontShaperFont {
    kbts_font font;
};

static bool is_loadable(const u8* data, i64 size) {
    return data != nullptr && size > 0 && size <= INT_MAX;
}

FontShaperFont* font_shaper_font_new(FlArena* arena, const u8* data, i64 size) {
    if (!is_loadable(data, size)) {
        return nullptr;
    }

    FontShaperFont* out = arena_alloc_zero(arena, FontShaperFont);
    kbts_load_font_state state = { 0 };
    int scratch_size = 0;
    int output_size = 0;
    const kbts_load_font_error error
        = kbts_LoadFont(&out->font, &state, (void*)data, (int)size, 0, &scratch_size, &output_size);
    if (error == KBTS_LOAD_FONT_ERROR_NEED_TO_CREATE_BLOB) {
        void* scratch = fl_heap_alloc((u64)scratch_size);
        void* output = fl_arena_alloc_raw(arena, (u64)output_size, _Alignof(max_align_t));
        kbts_PlaceBlob(&out->font, &state, scratch, output);
        fl_heap_free(scratch);
    }
    return kbts_FontIsValid(&out->font) ? out : nullptr;
}
