#include "../src/internal.h"
#include "../src/primitive_rect.h"
#include "utest.h"

#ifdef _WIN32
#include <malloc.h>  // alloca
#endif

struct FlContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generate a basic area

UTEST(Primitives, generate_box) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    PrimitiveRect rect = {0};

    rect.pos = (FlVec2){0.0f, 0.0f};
    rect.size = (FlVec2){10.0f, 20.0f};

    PrimitiveRect_generate_render_data(ctx, &rect);

    fl_context_destroy(ctx);
    fl_destroy(state);
}
