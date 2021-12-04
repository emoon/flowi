#include "../src/flowi_font.h"
#include "../src/internal.h"
#include "../src/linear_allocator.h"
#include "utest.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void init_allocator(LinearAllocator* alloc, const char* name, int size) {
    u8* data = malloc(size);
    memset(data, 0xcd, size);  // init memory to cdcd for uncleared memory
    LinearAllocator_init(alloc, name, data, size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, init) {
    LinearAllocator alloc;
    init_allocator(&alloc, "test_init", 20);
    ASSERT_TRUE(strcmp(alloc.id, "test_init") == 0);
    ASSERT_TRUE(alloc.start_data == alloc.start_data);
    ASSERT_TRUE(alloc.end_data == alloc.start_data + 20);
    ASSERT_TRUE(alloc.current_data == alloc.start_data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, alloc_single_byte) {
    LinearAllocator alloc;
    init_allocator(&alloc, "test", 20);
    u8* data = LinearAllocator_alloc(&alloc, u8);

    // make sure we have incremented pointers enough
    ASSERT_TRUE(alloc.current_data == data + 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, rewind) {
    LinearAllocator alloc;
    init_allocator(&alloc, "test", 20);

    // allocate some data
    LinearAllocator_alloc(&alloc, u8);
    LinearAllocator_alloc(&alloc, u16);
    LinearAllocator_alloc_array(&alloc, u16, 2);

    LinearAllocator_rewind(&alloc);

    // Make sure we are back at the start after rewind
    ASSERT_TRUE(alloc.current_data == alloc.start_data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, out_of_memory) {
    LinearAllocator alloc;
    init_allocator(&alloc, "test", 20);
    u8* data = LinearAllocator_alloc_array(&alloc, u8, 21);
    ASSERT_TRUE(data == NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, memory_pattern) {
    LinearAllocator alloc;
    init_allocator(&alloc, "test", 20);
    u8* data = LinearAllocator_alloc_array(&alloc, u8, 2);
    u8* data_2 = LinearAllocator_alloc_array_zero(&alloc, u8, 2);

    ASSERT_TRUE(data[0] == 0xcd);
    ASSERT_TRUE(data[1] == 0xcd);

    ASSERT_TRUE(data_2[0] == 0x00);
    ASSERT_TRUE(data_2[1] == 0x00);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(LinearAllocator, alignment) {
    LinearAllocator alloc;
    init_allocator(&alloc, "test", 20);

    // allocate until we have an uneven pointer
    for (int i = 0; i < 8; ++i) {
        u8* data = LinearAllocator_alloc(&alloc, u8);
        if (((uintptr_t)data) & 1) {
            break;
        }
    }

    // Allocate a u64 value that should be 8 byte alinged
    u64* data = LinearAllocator_alloc(&alloc, u64);

    // make sure we have aligned pointer
    ASSERT_TRUE((((uintptr_t)data) & 0x7) == 0);
}

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
    fl_create(NULL);
    // do your own thing
    return utest_main(argc, argv);
}
