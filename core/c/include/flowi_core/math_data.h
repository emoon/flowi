
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

typedef struct FlRect {
    int x;
    int y;
    int width;
    int height;
} FlRect;

// Used for setting a position in pixel space
typedef struct FlVec2 {
    // x position
    float x;
    // y position
    float y;
} FlVec2;

// Used for setting a position in pixel space
typedef struct FlIVec2 {
    // x position
    int x;
    // y position
    int y;
} FlIVec2;

#ifdef __cplusplus
}
#endif
