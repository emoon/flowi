
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "manual.h"
#include "math_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FlLayoutDirection {
    FlLayoutDirection_Horizontal = 0,
    FlLayoutDirection_Verticial = 1,
} FlLayoutDirection;

typedef enum FlSizeType {
    FlSizeType_Fixed = 0,
    FlSizeType_Stretch = 1,
} FlSizeType;

// LayoutMode make it possible to select how ui elements are being layed out.
typedef enum FlLayoutMode {
    // Automatic (default) will use [LayoutArea] to do automatic positining. See [LayoutArea] for more info on how to
    // use this.
    FlLayoutMode_Automatic = 0,
    // User will have to use the [Ui::set_position]
    FlLayoutMode_Manual = 1,
} FlLayoutMode;

typedef struct FlSizing {
    int value;
    FlSizeType value_type;
} FlSizing;

typedef uint64_t FlLayoutAreaId;

typedef struct FlLayoutArea {
    FlString name;
    FlSizing width;
    FlSizing height;
    FlLayoutDirection direction;
} FlLayoutArea;

#include "layout.inl"

#ifdef __cplusplus
}
#endif
