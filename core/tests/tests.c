#include "../src/flowi_font.h"
#include "../src/internal.h"
#include "../src/linear_allocator.h"
#include "utest.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlContext* g_ctx = NULL;


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Io, load_file_fail) {
    u32 size = 0;
    const u8* data = Io_load_file_to_memory("dummy_not_found", &size);
    ASSERT_TRUE(data == NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Io, load_file_ok) {
    u32 size = 0;
    const u8* data = Io_load_file_to_memory("data/montserrat-regular.ttf", &size);

    ASSERT_TRUE(data != NULL);
    ASSERT_TRUE(size == 245708);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Font, load_failed) {
    static u16 t[] = {0x0020, 0x0127};
    static FlGlyphRange font_range = {(u16*)&t, 2};

    FlFont font_id = fl_font_from_file("unable_to_load.bin", 12, FlFontBuildMode_Immediate,
                                       FlFontAtlasMode_PrebildGlyphs, FlFontGlyphPlacementMode_Basic, &font_range);

    // Expect loading fail
    ASSERT_TRUE(font_id == -1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Font, generate_font_latin) {
    static u16 t[] = {32, 127};
    static FlGlyphRange font_range = {(u16*)&t, 2};

    FlFont font_id = fl_font_from_file("data/montserrat-regular.ttf", 36, FlFontBuildMode_Immediate,
                                       FlFontAtlasMode_PrebildGlyphs, FlFontGlyphPlacementMode_Basic, &font_range);

    // Expect loading to work
    ASSERT_TRUE(font_id == 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_STATE();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, const char* const argv[]) {
	// Create a global context that we can use in all tests
	FlGlobalState* global = fl_create(NULL);
	g_ctx = fl_context_create(global);
    return utest_main(argc, argv);
}
