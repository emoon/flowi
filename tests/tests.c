#include "../src/internal.h"
#include "../src/linear_allocator.h"
#include "utest.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Malloc based allocator. We should use tslf or similar in a sandbox, but this is atleast in one place

static void* alloc_malloc(void* user_data, u64 size) {
    FL_UNUSED(user_data);
    return malloc(size);
}

static void* realloc_malloc(void* user_data, void* ptr, u64 size) {
    FL_UNUSED(user_data);
    return realloc(ptr, size);
}

static void free_malloc(void* user_data, void* ptr) {
    FL_UNUSED(user_data);
    free(ptr);
}

static void memory_error(void* user_data, const char* text, int text_len) {
    FL_UNUSED(user_data);
    FL_UNUSED(text);
    FL_UNUSED(text_len);
}

FlAllocator g_malloc_allocator = {
    FlAllocatorError_Exit, NULL, memory_error, alloc_malloc, NULL, realloc_malloc, free_malloc,
};

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
