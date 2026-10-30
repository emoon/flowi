#pragma once

#include "types.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// FlString, its inline helpers and the FL_S() / FL_S_() / FL_S_UTF8() literal macros come from the
// public header. Do NOT redefine FlString here - pin its layout with a _Static_assert instead.
#include <flowi/core/string.h>

struct FlArena;

// The generated service header is the single public source of the parse-status enum, the six
// parse-result PODs and FlStringBuilder. It must NOT redefine them - their layouts are pinned
// with _Static_asserts instead.
#include <flowi/string/string.h>

// offsetof on the length bitfield is not valid C, so only data is offset-pinned.
_Static_assert(sizeof(FlString) == 16, "FlString size drift vs <flowi/core/string.h>");
_Static_assert(offsetof(FlString, data) == 0, "FlString.data offset drift");

// FlStringBuilder crosses the FFI by value.
_Static_assert(sizeof(FlStringBuilder) == 16, "FlStringBuilder size drift vs <flowi/string/string.h>");
_Static_assert(offsetof(FlStringBuilder, arena) == 0, "FlStringBuilder.arena offset drift");
_Static_assert(offsetof(FlStringBuilder, start_pos) == 8, "FlStringBuilder.start_pos offset drift");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal aliases mapping the short core names onto the canonical Fl* names

#define StringParseStatus FlStringParseStatus
#define StringParseStatus_Success FlStringParseStatus_Success
#define StringParseStatus_InvalidFormat FlStringParseStatus_InvalidFormat
#define StringParseStatus_Overflow FlStringParseStatus_Overflow
#define StringParseStatus_Underflow FlStringParseStatus_Underflow
#define StringParseStatus_Empty FlStringParseStatus_Empty
#define StringParseStatus_InvalidHexPrefix FlStringParseStatus_InvalidHexPrefix

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Result types using the shared enum

#define StringParseResultU64 FlStringParseResultU64
#define StringParseResultU32 FlStringParseResultU32
#define StringParseResultI64 FlStringParseResultI64
#define StringParseResultI32 FlStringParseResultI32
#define StringParseResultF64 FlStringParseResultF64
#define StringParseResultF32 FlStringParseResultF32

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct StringArray {
    FlString* items;
    int count;
    u32 capacity;
} StringArray;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Declared in <flowi/string/string.h>; the va_list variant is not part of the generated surface.
FlStringBuilder sb_vappendf(FlStringBuilder sb, const char* format, va_list args);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// sprintf.h includes this header back for FlString; both are #pragma once.

#include "sprintf.h"
