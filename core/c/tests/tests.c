#include <flowi_core/font.h>
#include "../src/internal.h"
#include "../src/linear_allocator.h"
#include "utest.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Io, load_file_fail) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    u32 size = 0;
    const u8* data = Io_load_file_to_memory(ctx, "dummy_not_found", &size);
    ASSERT_TRUE(data == NULL);

    fl_context_destroy(ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Io, load_file_ok) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    u32 size = 0;
    u8* data = Io_load_file_to_memory(ctx, "data/montserrat-regular.ttf", &size);

    ASSERT_TRUE(data != NULL);
    ASSERT_TRUE(size == 245708);

    // TODO: Allocator
    FlAllocator_free(state->global_allocator, data);

    fl_context_destroy(ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_STATE();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, const char* const argv[]) {
    int status = utest_main(argc, argv);
    return status;
}
