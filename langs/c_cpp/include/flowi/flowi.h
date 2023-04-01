#include "button.h"
#include "font.h"
#include "image.h"
#include "io.h"
#include "item.h"
#include "layout.h"
#include "menu.h"
#include "painter.h"
#include "style.h"
#include "text.h"
#include "ui.h"
#include "window.h"

/*
struct App;

typedef struct FlApp {
   struct FlInternalData* priv;
    bool (*main_loop)(FlMainLoopCallback callback, void* user_data);
    struct FlButtonApi* (*button_get_api)(struct FlInternalData* data, int api_version);
    struct FlCursorApi* (*cursor_get_api)(struct FlInternalData* data, int api_version);
    struct FlFontApi* (*font_get_api)(struct FlInternalData* data, int api_version);
    struct FlImageApi* (*image_get_api)(struct FlInternalData* data, int api_version);
    struct FlIoApi* (*io_get_api)(struct FlInternalData* data, int api_version);
    struct FlItemApi* (*item_get_api)(struct FlInternalData* data, int api_version);
    struct FlMenuApi* (*menu_get_api)(struct FlInternalData* data, int api_version);
    struct FlPainterApi* (*painter_get_api)(struct FlInternalData* data, int api_version);
    struct FlStyleApi* (*style_get_api)(struct FlInternalData* data, int api_version);
    struct FlTextApi* (*text_get_api)(struct FlInternalData* data, int api_version);
    struct FlUiApi* (*ui_get_api)(struct FlInternalData* data, int api_version);
    struct FlWindowApi* (*window_get_api)(struct FlInternalData* data, int api_version);
} FlApp;

FL_INLINE bool fl_application_create(FlApplicationSettings* settings) {
    FlApp* api = fl_application_create_impl(settings, 0);
    g_flowi_button_api = api->button_get_api(api->priv, 0);
    g_flowi_cursor_api = api->cursor_get_api(api->priv, 0);
    g_flowi_font_api = api->font_get_api(api->priv, 0);
    g_flowi_image_api = api->image_get_api(api->priv, 0);
    g_flowi_io_api = api->io_get_api(api->priv, 0);
    g_flowi_item_api = api->item_get_api(api->priv, 0);
    g_flowi_menu_api = api->menu_get_api(api->priv, 0);
    g_flowi_painter_api = api->painter_get_api(api->priv, 0);
    g_flowi_style_api = api->style_get_api(api->priv, 0);
    g_flowi_text_api = api->text_get_api(api->priv, 0);
    g_flowi_ui_api = api->ui_get_api(api->priv, 0);
    g_flowi_window_api = api->window_get_api(api->priv, 0);
    return api;
}
*/


