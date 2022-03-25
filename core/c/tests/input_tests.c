#include "../src/internal.h"
#include "utest.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Input, mouse_button_down) {
    struct FlGlobalState* state = fl_create(NULL);
    FlContext* ctx = fl_context_create(state);

    // click left mouse button and mouse at 10, 10
    fl_set_mouse_pos_state(ctx, (FlVec2){10.0f, 10.0f}, true, false, false);

    fl_frame_begin(ctx, 640, 480, 1.0f / 60.f);

    ASSERT_TRUE(ctx->mouse.clicked[0]);
    ASSERT_NEAR(ctx->mouse.pos.x, 10.0f, 0.01f);
    ASSERT_NEAR(ctx->mouse.pos.y, 10.0f, 0.01f);

    fl_frame_end(ctx);

    fl_context_destroy(ctx);
    fl_destroy(state);
}