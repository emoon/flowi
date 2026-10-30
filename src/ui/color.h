#pragma once

#include <core/types.h>
#include <flowi/core/math_data.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline FlColor ui_color(u8 r, u8 g, u8 b, u8 a) {
    return (FlColor) { .r = r, .g = g, .b = b, .a = a };
}
