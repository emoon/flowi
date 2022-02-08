
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "idx.h"
#include "manual.h"

struct FlContext;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlRect {
    int x;
    int y;
    int width;
    int height;
} FlRect;

// Used for setting a position in pixel space
typedef struct FlPos {
    // x position
    float x;
    // y position
    float y;
} FlPos;

#ifdef __cplusplus
}
#endif
