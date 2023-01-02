#include "font.h"
#include "image.h"
#include "layout.h"
#include "style.h"
#include "text.h"
#include "ui.h"
#include "window.h"

struct FlInternalData;

typedef struct FlContext {
    struct FlInternalData* priv;
    struct FlCursorApi* (*cursor_get_api)(struct FlInternalData* data, int api_version);
    struct FlFontApi* (*font_get_api)(struct FlInternalData* data, int api_version);
    struct FlImageApi* (*image_get_api)(struct FlInternalData* data, int api_version);
    struct FlStyleApi* (*style_get_api)(struct FlInternalData* data, int api_version);
    struct FlTextApi* (*text_get_api)(struct FlInternalData* data, int api_version);
    struct FlUiApi* (*ui_get_api)(struct FlInternalData* data, int api_version);
    struct FlWindowApi* (*window_get_api)(struct FlInternalData* data, int api_version);
} FlContext;

FL_INLINE struct FlCursorApi* fl_cursor_api(FlContext* ctx) { return (ctx->cursor_get_api)(ctx->priv, 0); }
FL_INLINE struct FlFontApi* fl_font_api(FlContext* ctx) { return (ctx->font_get_api)(ctx->priv, 0); }
FL_INLINE struct FlImageApi* fl_image_api(FlContext* ctx) { return (ctx->image_get_api)(ctx->priv, 0); }
FL_INLINE struct FlStyleApi* fl_style_api(FlContext* ctx) { return (ctx->style_get_api)(ctx->priv, 0); }
FL_INLINE struct FlTextApi* fl_text_api(FlContext* ctx) { return (ctx->text_get_api)(ctx->priv, 0); }
FL_INLINE struct FlUiApi* fl_ui_api(FlContext* ctx) { return (ctx->ui_get_api)(ctx->priv, 0); }
FL_INLINE struct FlWindowApi* fl_window_api(FlContext* ctx) { return (ctx->window_get_api)(ctx->priv, 0); }

