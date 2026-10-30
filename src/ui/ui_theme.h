#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// flowi default theme constants - internal
//
// The hand-written default-theme color constants: the fallback palette used when no theme.json loads. Colors are
// { r, g, b, a } (FlColor, math_data).
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ui_types.h"
#include <flowi/ui/theme.h> // FlThemeColors, FlThemeSlot, FlThemeOverride (api_gen)

#define FL_THEME_DEFAULT_INTERACTIVE_BASE { 201, 22, 38, 255 }
#define FL_THEME_DEFAULT_INTERACTIVE_HOVER { 220, 57, 70, 255 }
#define FL_THEME_DEFAULT_INTERACTIVE_PRESS { 171, 19, 32, 255 }
#define FL_THEME_DEFAULT_INTERACTIVE_FOCUS { 255, 255, 255, 255 }
#define FL_THEME_DEFAULT_INTERACTIVE_DISABLED { 201, 22, 38, 128 }

#define FL_THEME_DEFAULT_SURFACE_BASE { 14, 14, 16, 255 }
#define FL_THEME_DEFAULT_SURFACE_ELEVATED { 32, 32, 36, 255 }
#define FL_THEME_DEFAULT_SURFACE_OVERLAY { 10, 10, 12, 255 }
#define FL_THEME_DEFAULT_SURFACE_SELECTED { 201, 22, 38, 255 }

#define FL_THEME_DEFAULT_BORDER_DEFAULT { 40, 40, 46, 255 }
#define FL_THEME_DEFAULT_BORDER_FOCUS { 230, 80, 92, 255 }
#define FL_THEME_DEFAULT_BORDER_SEPARATOR { 40, 40, 46, 255 }

#define FL_THEME_DEFAULT_TEXT_PRIMARY { 240, 240, 240, 255 }
#define FL_THEME_DEFAULT_TEXT_SECONDARY { 153, 153, 153, 255 }
#define FL_THEME_DEFAULT_TEXT_ACCENT { 201, 22, 38, 255 }

#define FL_THEME_DEFAULT_FEEDBACK_SUCCESS { 76, 175, 80, 255 }
#define FL_THEME_DEFAULT_FEEDBACK_WARNING { 255, 152, 0, 255 }
#define FL_THEME_DEFAULT_FEEDBACK_ERROR { 244, 67, 54, 255 }
#define FL_THEME_DEFAULT_FEEDBACK_INFO { 33, 150, 243, 255 }
