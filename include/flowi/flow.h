#include "application.h"
#include "context.h"
#include "debug.h"
#include "error.h"
#include "font.h"
#include "image.h"
#include "layout.h"
#include "math_data.h"
#include "render_commands.h"
#include "style.h"
#include "ui.h"
#include "window.h"


struct FlInternalData;

typedef struct FlContext {
    struct FlInternalData* private;
    struct FlFontApi* font_get_api(struct FlInternalData* data, int api_version);
    struct FlImageApi* image_get_api(struct FlInternalData* data, int api_version);
    struct FlStyleApi* style_get_api(struct FlInternalData* data, int api_version);
    struct FlUiApi* ui_get_api(struct FlInternalData* data, int api_version);
}
static FL_INLINE fl_font(FlContext* ctx) { return font_get_api(ctx->private, 0); }
static FL_INLINE fl_image(FlContext* ctx) { return image_get_api(ctx->private, 0); }
static FL_INLINE fl_style(FlContext* ctx) { return style_get_api(ctx->private, 0); }
static FL_INLINE fl_ui(FlContext* ctx) { return ui_get_api(ctx->private, 0); }

