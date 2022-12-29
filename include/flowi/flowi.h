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
    struct FlInternalData* priv;
    struct FlFontApi* (*font_get_api)(struct FlInternalData* data, int api_version);
    struct FlImageApi* (*image_get_api)(struct FlInternalData* data, int api_version);
    struct FlStyleApi* (*style_get_api)(struct FlInternalData* data, int api_version);
    struct FlUiApi* (*ui_get_api)(struct FlInternalData* data, int api_version);
} FlContext;

FL_INLINE struct FlFontApi* fl_font_api(FlContext* ctx) { return (ctx->font_get_api)(ctx->priv, 0); }
FL_INLINE struct FlImageApi* fl_image_api(FlContext* ctx) { return (ctx->image_get_api)(ctx->priv, 0); }
FL_INLINE struct FlStyleApi* fl_style_api(FlContext* ctx) { return (ctx->style_get_api)(ctx->priv, 0); }
FL_INLINE struct FlUiApi* fl_ui_api(FlContext* ctx) { return (ctx->ui_get_api)(ctx->priv, 0); }

