#pragma once

// Export/visibility annotation carried by every public `fl_*` prototype. On Windows it is the export
// mechanism itself: dllexport while building the library, dllimport while consuming it, keyed off
// FL_API_IMPLEMENTATION. Elsewhere it is default visibility, and the linker export list decides the
// final dynamic symbol table.
//
// This header ships to external consumers and must stand alone, so it tests the raw compiler-defined
// _WIN32 rather than including core.h for PLATFORM_WINDOWS.
#ifndef FL_API
#if defined(_WIN32)
#if defined(FL_API_IMPLEMENTATION)
#define FL_API __declspec(dllexport)
#else
#define FL_API __declspec(dllimport)
#endif
#else
#define FL_API __attribute__((visibility("default")))
#endif
#endif
