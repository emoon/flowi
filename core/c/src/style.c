#include "style.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <flowi_core/error.h>
#include "internal.h"
#include "style_internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Default style

static FlStyle s_default_style = {
    "flowi_default",
    .border =
        {
            .border_radius =
                {
                    {.value = 0.0f, FlLengthPercentType_Length},
                    {.value = 0.0f, FlLengthPercentType_Length},
                    {.value = 0.0f, FlLengthPercentType_Length},
                    {.value = 0.0f, FlLengthPercentType_Length},
                },

            .colors = {FL_RGB_RED, FL_RGB_RED, FL_RGB_RED, FL_RGB_RED},
            .width = 0,
            .active = false,
        },

    .padding = {4, 4, 4, 4},
    .margin = {4, 4, 4, 4},
    .current_font = 0,
    .background_color = FL_RGB_WHITE,
    .text_color = FL_RGB_BLACK,
    .font_size = 0,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create a style to apply changes to with an optional name

FlStyle* fl_style_create_name_len(struct FlContext* ctx, const char* name, int name_len) {
	FL_UNUSED(name);
	FL_UNUSED(name_len);

    // TODO: Separate allocator
    StyleInternal* style_internal = malloc(sizeof(StyleInternal));
    assert(style_internal != NULL);
    assert(ctx->style_count < FL_MAX_STYLES);
    memcpy(&style_internal->style, &s_default_style, sizeof(FlStyle));
    style_internal->has_generated_diff = false;

    // TODO: Dynamic array
    ctx->styles[ctx->style_count++] = style_internal;

    return &style_internal->style;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get the default style, not intended for

FlStyle* fl_style_get_default(struct FlContext* ctx) {
	FL_UNUSED(ctx);

    return &s_default_style;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Select the style to be used, to end using the style use 'fl_pop_style()'

void fl_style_push(FlContext* ctx, FlStyle* style) {
    int style_stack_depth = ctx->style_stack_depth;

    // as it doesn't make any sense to push the default style we will just exit here
    if (style == &s_default_style) {
        return;
    }

    if (ctx->style_stack_depth >= FL_STYLE_DEPTH) {
        ERROR_ADD(FlError_Style,
                  "Unable to push style %s as stack depth (%d) is full. Are you missing a pop of the style(s)?",
                  style->name, FL_STYLE_DEPTH);
        return;
    }

    ctx->style_stack[style_stack_depth++] = (StyleInternal*)style;
    ctx->style_stack_depth = style_stack_depth;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pops the current style

void fl_style_pop(FlContext* ctx) {
    const int depth = ctx->style_stack_depth - 1;
    ctx->style_stack_depth = FL_MAX(depth, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void build_diff_bits(u8* FL_RESTRICT bits, const u8* FL_RESTRICT def, const u8* FL_RESTRICT style) {
    for (u32 i = 0; i < sizeof(FlStyle); ++i) {
        const u8 d = *def++;
        const u8 s = *style++;
        *bits++ = d != s ? 0 : 1;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mark the end of style changes

void fl_style_end_changes(FlStyle* end_style) {
    StyleInternal* style_internal = (StyleInternal*)end_style;
    const u8* style = (const u8*)&style_internal->style;
    const u8* def_style = (const u8*)&s_default_style;

    // if we are apply changes to the default style, we don'n need to calculate the changes
    if (style == def_style) {
        return;
    }

    style_internal->has_generated_diff = true;
    build_diff_bits(style_internal->diff_bits, def_style, style);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// If something in the style differs between target and src they will be copied to the target

static void apply_style_diff(u8* FL_RESTRICT target, const u8* FL_RESTRICT src, const u8* diff) {
    for (u32 i = 0; i < sizeof(FlStyle); ++i) {
        const u8 t = *target;
        const u8 s = *src++;
        const u8 select = *diff++;
        *target++ = select != 0 ? t : s;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get the current style which is based on what has been pushed on the style stack using push/pop

void fl_style_get_current(struct FlContext* ctx, FlStyle* style) {
    const u8* def_style = (u8*)&s_default_style;

    // first we copy the default style to the style as this will be our base line
    memcpy(style, def_style, sizeof(FlStyle));

    for (int i = 0, count = ctx->style_stack_depth; i < count; ++i) {
        StyleInternal* int_style = ctx->style_stack[i];
        apply_style_diff((u8*)&int_style->style, def_style, int_style->diff_bits);
    }

    style->name = "flowi_merged";  // indicated this is a merged style
}
