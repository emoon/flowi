#pragma once

// Allow usage for fopen/free/etc/functions
#define FL_ALLOW_STDIO 1

// Use Freetype to generate fonts
#define FL_FONTLIB_FREETYPE

// Use stb_truetype to generate fonts
//#define FL_FONTLIB_STBTYPE
//


#if !defined(FL_WCHAR_SIZE)
#define FL_WCHAR unsigned short;
#elif FL_WCHAR_SIZE == 2
#define FL_WCHAR unsigned short;
#elif FL_WCHAR_SIZE == 4
#define FL_WCHAR unsigned int;
#else
#error "Unsupported FL_WCHAR_SIZE. Can only be 2 or 4"
#endif

#define FL_WCHAR_SIZE

#if !defined(FL_FONTLIB_FREETYPE) && !defined(FL_FONTLIB_STBTYPE)
#error "Must have at least one font-generator"
#endif

#if defined(FL_FONTLIB_FREETYPE) && defined(FL_FONTLIB_STBTYPE)
#error "Can only have one font-generator"
#endif

