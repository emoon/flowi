#pragma once

#include <stdarg.h>

// The part of vendored stb_sprintf (sprintf.c) that first-party code builds on.

#ifndef STB_SPRINTF_MIN
#define STB_SPRINTF_MIN 512 // how many characters per callback
#endif
typedef char* STBSP_SPRINTFCB(const char* buf, void* user, int len);

extern int stbsp_vsprintfcb(STBSP_SPRINTFCB* callback, void* user, char* buf, char const* fmt, va_list va);
