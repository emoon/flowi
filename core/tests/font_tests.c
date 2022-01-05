#include "utest.h"
#include "../src/font.h"

struct FlContext;

extern struct FlContext* g_ctx;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Font, load_failed) {
    FlFont font_id = fl_font_create_from_file(g_ctx, "unable_to_load.bin", 12, FlFontGlyphPlacementMode_Auto);

    // Expect loading fail
    ASSERT_TRUE(font_id == -1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Font, load_font_ok) {
    FlFont font_id = fl_font_create_from_file(g_ctx, "data/montserrat-regular.ttf", 36, FlFontGlyphPlacementMode_Auto);

    // Expect loading to work
    ASSERT_TRUE(font_id == 0);

    fl_font_destroy(g_ctx, font_id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Font, load_font_ok_2) {
}



