#include "utest.h"
#include "../src/flowi.h"
#include "../src/internal.h"
#include "../src/style.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validate that a new style has the default values

UTEST(Style, default_create) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

	FlStyle* style = fl_style_create(ctx, "test");
	FlStyle* default_style = fl_style_get_default(ctx);

	//int t = memcmp(style, default_style, sizeof(FlStyle));
	//printf("defaul_create_style %d\n", t);

	ASSERT_TRUE(memcmp(style, default_style, sizeof(FlStyle)) == 0);

	fl_context_destroy(ctx);
	fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validate that changing some data in the new style will not be the same as the default one

UTEST(Style, diff_default) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

	FlStyle* style = fl_style_create(ctx, "test");
	FlStyle* default_style = fl_style_get_default(ctx);

	style->border = (FlBorder) {
		.active = true,
	};

	ASSERT_TRUE(memcmp(style, default_style, sizeof(FlStyle)) != 0);

	fl_context_destroy(ctx);
	fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validate that changing some data in the new style will not be the same as the default one

UTEST(Style, push_pop_style) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

	FlStyle* style = fl_style_create(ctx, "test");
	fl_style_push(ctx, style);

	// expect stack depth to be 1 at this point
	ASSERT_TRUE(ctx->style_stack_depth == 1);

	fl_style_pop(ctx);

	// expect stack depth to be 0 at this point
	ASSERT_TRUE(ctx->style_stack_depth == 0);

	fl_style_pop(ctx);

	// Validate that the stack is still at depth 0 with non-needed pop
	ASSERT_TRUE(ctx->style_stack_depth == 0);

	fl_context_destroy(ctx);
	fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validate that we will never push more than max number of styles on the stack

UTEST(Style, overflow_style_stack) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

	FlStyle* style = fl_style_create(ctx, "test");

	for (int i = 0; i < FL_STYLE_DEPTH * 2; ++i) {
		fl_style_push(ctx, style);
	}

	// Validate that we only added the max amount number of styles
	ASSERT_TRUE(ctx->style_stack_depth == FL_STYLE_DEPTH);

	for (int i = 0; i < FL_STYLE_DEPTH; ++i) {
		fl_style_pop(ctx);
	}

	// expect stack depth to be 0 at this point
	ASSERT_TRUE(ctx->style_stack_depth == 0);

	fl_context_destroy(ctx);
	fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Trypes to push the default style and we expect nothing to change as this isn't supported

UTEST(Style, push_default) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

	fl_style_push(ctx, fl_style_get_default(ctx));
	ASSERT_TRUE(ctx->style_stack_depth == 0);

	fl_context_destroy(ctx);
	fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Trypes to push the default style and we expect nothing to change as this isn't supported

UTEST(Style, push_style_1) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

	FlStyle* style = fl_style_create(ctx, "test");
	FlStyle* default_style = fl_style_get_default(ctx);

	// test change setting
	style->background_color = 0xffff;
	fl_style_end_changes(style);

	FlStyle current;
	fl_style_get_current(ctx, &current);
	ASSERT_TRUE(current.background_color == default_style->background_color);

	fl_style_push(ctx, style);
	fl_style_get_current(ctx, &current);
	ASSERT_TRUE(current.background_color == style->background_color);

	fl_style_pop(ctx);
	fl_style_get_current(ctx, &current);
	ASSERT_TRUE(current.background_color == default_style->background_color);

	fl_context_destroy(ctx);
	fl_destroy(state);
}

