#include "style.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Default style

static FlStyle s_default_style = {
	.priv = 0,
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
	FlStyle* style = malloc(sizeof(FlStyle));
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

// end changes made to the current style
void fl_style_end_changes(FlStyle* style);

// Called before making changes to an existing style after making it with end_changes
void fl_style_begin_changes(FlStyle* style);

// Select the style to be used, to end using the style use 'fl_pop_style()'
void fl_push_style(FlStyle* style);

// Pops the current style
void fl_pop_style();


