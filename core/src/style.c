#include "style.h"
#include "internal.h"
#include "style_internal.h"
#include "../include/error.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Default style

static FlStyle s_default_style = {
	"flowi_default",
	.border = {
		.border_radius = {
			{ .value = 0.0f, FlLengthPercentType_Length },
			{ .value = 0.0f, FlLengthPercentType_Length },
			{ .value = 0.0f, FlLengthPercentType_Length },
			{ .value = 0.0f, FlLengthPercentType_Length },
		},

		.colors = { FL_RGB_RED, FL_RGB_RED, FL_RGB_RED, FL_RGB_RED },
		.width = 0,
		.active = false,
	},

	.padding = { .sides = { 4, 4, 4, 4 } },
	.margin = { .sides = { 4, 4, 4, 4 } },
	.current_font = 0,
	.background_color = FL_RGB_WHITE,
	.text_color = FL_RGB_BLACK,
	.font_size = 0,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create a style to apply changes to with an optional name

FlStyle* fl_style_create_name_len(struct FlContext* ctx, const char* name, int name_len) {
	int total_size = sizeof(FlStyle);
	FlStyle* style = malloc(total_size);
	assert(style != NULL);
	assert(ctx->style_count < FL_MAX_STYLES);
	memcpy(style, &s_default_style, sizeof(FlStyle));

	// TODO: Dynamic array
	ctx->styles[ctx->style_count++] = style;

	return style;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get the default style, not intended for

FlStyle* fl_style_get_default(struct FlContext* ctx) {
	(void)ctx;
	return &s_default_style;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Select the style to be used, to end using the style use 'fl_pop_style()'

void fl_style_push(FlContext* ctx, FlStyle* style) {
	int style_stack_depth = ctx->style_stack_depth;

	if (ctx->style_stack_depth >= FL_STYLE_DEPTH) {
		ERROR_ADD(FlError_Style, "Unable to push style %s as stack depth (%d) is full. Are you missing a pop of the style(s)?", style->name, FL_STYLE_DEPTH);
		return;
	}

	ctx->style_stack[style_stack_depth++] = style;
	ctx->style_stack_depth = style_stack_depth;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pops the current style

void fl_style_pop(FlContext* ctx) {
	const int depth = ctx->style_stack_depth - 1;
	ctx->style_stack_depth = depth >= 0 ? depth : 0;
}

