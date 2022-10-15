
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "context.h"
#include "idx.h"
#include "manual.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FlError {
    FlError_None = 0,
    FlError_Io = 1,
    FlError_Memory = 2,
    FlError_Font = 3,
    FlError_Style = 4,
    FlError_Image = 5,
    FlError_Utf8Malformed = 6,
    FlError_Generic = 7,
} FlError;

#include "error.inl"

#ifdef __cplusplus
}
#endif
