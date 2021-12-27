#include "utest.h"
#include "../src/flowi.h"
#include "../src/internal.h"
#include "../src/style.h"

extern FlContext* g_ctx;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validate that a new style has the default values

UTEST(Style, default_create) {
	FlStyle* style = fl_style_create(g_ctx, "test");
	FlStyle* default_style = fl_style_get_default(g_ctx);

	int t = memcmp(style, default_style, sizeof(FlStyle));
	printf("defaul_create_style %d\n", t);

	ASSERT_TRUE(memcmp(style, default_style, sizeof(FlStyle)) == 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validate that changing some data in the new style will not be the same as the default one

UTEST(Style, diff_default) {
	FlStyle* style = fl_style_create(g_ctx, "test");
	FlStyle* default_style = fl_style_get_default(g_ctx);

	style->border = (FlBorder) {
		.active = true,
	};

	ASSERT_TRUE(memcmp(style, default_style, sizeof(FlStyle)) != 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validate that changing some data in the new style will not be the same as the default one

UTEST(Style, push_pop_style) {
	FlStyle* style = fl_style_create(g_ctx, "test");
	fl_style_push(g_ctx, style);

	// expect stack depth to be 1 at this point
	ASSERT_TRUE(g_ctx->style_stack_depth == 1);

	fl_style_pop(g_ctx);

	// expect stack depth to be 0 at this point
	ASSERT_TRUE(g_ctx->style_stack_depth == 0);

	fl_style_pop(g_ctx);

	// Validate that the stack is still at depth 0 with non-needed pop
	ASSERT_TRUE(g_ctx->style_stack_depth == 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validate that we will never push more than max number of styles on the stack

UTEST(Style, overflow_style_stack) {
	FlStyle* style = fl_style_create(g_ctx, "test");

	for (int i = 0; i < FL_STYLE_DEPTH * 2; ++i) {
		fl_style_push(g_ctx, style);
	}

	// Validate that we only added the max amount number of styles
	ASSERT_TRUE(g_ctx->style_stack_depth == FL_STYLE_DEPTH);

	for (int i = 0; i < FL_STYLE_DEPTH; ++i) {
		fl_style_pop(g_ctx);
	}

	// expect stack depth to be 0 at this point
	ASSERT_TRUE(g_ctx->style_stack_depth == 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Trypes to push the default style and we expect nothing to change as this isn't supported

UTEST(Style, push_default) {
	fl_style_push(g_ctx, fl_style_get_default(g_ctx));
	ASSERT_TRUE(g_ctx->style_stack_depth == 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Trypes to push the default style and we expect nothing to change as this isn't supported

UTEST(Style, push_style_1) {
	FlStyle* style = fl_style_create(g_ctx, "test");
	FlStyle* default_style = fl_style_get_default(g_ctx);

	// test change setting
	style->background_color = 0xffff;
	fl_style_end_changes(style);

	FlStyle current;
	fl_style_get_current(g_ctx, &current);
	ASSERT_TRUE(current.background_color == default_style->background_color);

	fl_style_push(g_ctx, style);
	fl_style_get_current(g_ctx, &current);
	ASSERT_TRUE(current.background_color == style->background_color);

	fl_style_pop(g_ctx);
	fl_style_get_current(g_ctx, &current);
	ASSERT_TRUE(current.background_color == default_style->background_color);
}
