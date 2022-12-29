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

struct FlFontApi* fl_font_get_api(struct FlContext* ctx, int api_version);
struct FlImageApi* fl_image_get_api(struct FlContext* ctx, int api_version);
struct FlStyleApi* fl_style_get_api(struct FlContext* ctx, int api_version);
struct FlUiApi* fl_ui_get_api(struct FlContext* ctx, int api_version);

#define fl_font(ctx) fl_font_get_api(ctx, 0)
#define fl_image(ctx) fl_image_get_api(ctx, 0)
#define fl_style(ctx) fl_style_get_api(ctx, 0)
#define fl_ui(ctx) fl_ui_get_api(ctx, 0)

