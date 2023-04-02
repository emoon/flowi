#pragma once

#include <flowi/image.h>
#include <flowi/button.h>
#include <flowi/error.h>
#include <flowi/font.h>
#include <flowi/image.h>
#include <flowi/io.h>
#include <flowi/item.h>
#include <flowi/layout.h>
#include <flowi/menu.h>
#include <flowi/style.h>
#include <flowi/text.h>
#include <flowi/window.h>
//#include "../external/hashmap.h"
#include "command_buffer.h"
#include "string_allocator.h"
#include "layer.h"
#include "flowi_internal.h"
#include "handles.h"
#include "allocator.h"

#ifdef __cplusplus
extern "C" {
#endif


// If Flowi is compiled as dynamic library 
#if defined(FLOWI_DYNAMIC)
#define FL_PUBLIC_SYMBOL static
#else
#define FL_PUBLIC_SYMBOL extern "C"
#endif

struct GLFWwindow;

// These are for internal library wise functions. This header should never
// be included in the public headers!

#define ERROR_ADD(t, format, ...) Errors_add(t, __FILE__, __LINE__, format, __VA_ARGS__);
void Errors_add(FlError err, const char* filename, int line, const char* fmt, ...);

typedef struct FlInternalData {
    struct ImGuiContext* imgui_ctx;
    struct GLFWwindow* window;
    FlButtonApi button_api;
    FlCursorApi cursor_api;
    FlFontApi font_api;
    FlImageApi image_api;
    FlIoApi io_api;
    FlItemApi item_api;
    FlMenuApi menu_api;
    FlStyleApi style_api;
    FlTextApi text_api;
    FlUiApi ui_api;
    FlWindowApi window_api;
    LinearAllocator frame_allocator;
    StringAllocator string_allocator;
} FlInternalData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

