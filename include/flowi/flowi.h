#include "button.h"
#include "font.h"
#include "image.h"
#include "item.h"
#include "layout.h"
#include "menu.h"
#include "painter.h"
#include "style.h"
#include "text.h"
#include "ui.h"
#include "window.h"

struct FlInternalData;

typedef struct FlContext {
    struct FlInternalData* priv;
    struct FlButtonApi* (*button_get_api)(struct FlInternalData* data, int api_version);
    struct FlCursorApi* (*cursor_get_api)(struct FlInternalData* data, int api_version);
    struct FlFontApi* (*font_get_api)(struct FlInternalData* data, int api_version);
    struct FlImageApi* (*image_get_api)(struct FlInternalData* data, int api_version);
    struct FlItemApi* (*item_get_api)(struct FlInternalData* data, int api_version);
    struct FlMenuApi* (*menu_get_api)(struct FlInternalData* data, int api_version);
    struct FlPainterApi* (*painter_get_api)(struct FlInternalData* data, int api_version);
    struct FlStyleApi* (*style_get_api)(struct FlInternalData* data, int api_version);
    struct FlTextApi* (*text_get_api)(struct FlInternalData* data, int api_version);
    struct FlUiApi* (*ui_get_api)(struct FlInternalData* data, int api_version);
    struct FlWindowApi* (*window_get_api)(struct FlInternalData* data, int api_version);
} FlContext;

FL_INLINE struct FlButtonApi* fl_button_api(FlContext* ctx) { return (ctx->button_get_api)(ctx->priv, 0); }
FL_INLINE struct FlCursorApi* fl_cursor_api(FlContext* ctx) { return (ctx->cursor_get_api)(ctx->priv, 0); }
FL_INLINE struct FlFontApi* fl_font_api(FlContext* ctx) { return (ctx->font_get_api)(ctx->priv, 0); }
FL_INLINE struct FlImageApi* fl_image_api(FlContext* ctx) { return (ctx->image_get_api)(ctx->priv, 0); }
FL_INLINE struct FlItemApi* fl_item_api(FlContext* ctx) { return (ctx->item_get_api)(ctx->priv, 0); }
FL_INLINE struct FlMenuApi* fl_menu_api(FlContext* ctx) { return (ctx->menu_get_api)(ctx->priv, 0); }
FL_INLINE struct FlPainterApi* fl_painter_api(FlContext* ctx) { return (ctx->painter_get_api)(ctx->priv, 0); }
FL_INLINE struct FlStyleApi* fl_style_api(FlContext* ctx) { return (ctx->style_get_api)(ctx->priv, 0); }
FL_INLINE struct FlTextApi* fl_text_api(FlContext* ctx) { return (ctx->text_get_api)(ctx->priv, 0); }
FL_INLINE struct FlUiApi* fl_ui_api(FlContext* ctx) { return (ctx->ui_get_api)(ctx->priv, 0); }
FL_INLINE struct FlWindowApi* fl_window_api(FlContext* ctx) { return (ctx->window_get_api)(ctx->priv, 0); }

