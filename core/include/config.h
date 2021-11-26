#pragma once

// Allow usage for fopen/free/etc/functions
#define FL_ALLOW_STDIO 1

// Use Freetype to generate fonts
#define FL_FONTLIB_FREETYPE

// Use stb_truetype to generate fonts
//#define FL_FONTLIB_STBTYPE

#if !defined(FL_FONTLIB_FREETYPE) && !defined(FL_FONTLIB_STBTYPE)
#error "Must have at least one font-generator"
#endif

#if defined(FL_FONTLIB_FREETYPE) && defined(FL_FONTLIB_STBTYPE)
#error "Can only have one font-generator"
#endif

