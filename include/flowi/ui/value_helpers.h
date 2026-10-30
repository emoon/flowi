#pragma once

#include <flowi/core/math_data.h>
#include <flowi/core/string.h>
#include <flowi/ui/layout.h>

#define fl_color(r, g, b, a) ((FlColor) { (r), (g), (b), (a) })

#define fl_corner_radius(radius) ((FlCornerRadius) { (radius), (radius), (radius), (radius) })

#define fl_padding_all(padding) ((FlLayoutPadding) { (padding), (padding), (padding), (padding) })

#define fl_sizing_fit(...) \
    ((FlLayoutSizingAxis) { .size = { .min_max = { __VA_ARGS__ } }, .type = FlLayoutSizingType_Fit })

#define fl_sizing_grow(...) \
    ((FlLayoutSizingAxis) { .size = { .min_max = { __VA_ARGS__ } }, .type = FlLayoutSizingType_Grow })

#define fl_sizing_fixed(fixed_size)                                                \
    ((FlLayoutSizingAxis) { .size = { .min_max = { (fixed_size), (fixed_size) } }, \
                            .type = FlLayoutSizingType_Fixed })

#define fl_sizing_percent(percent_of_parent) \
    ((FlLayoutSizingAxis) { .size = { .percent = (percent_of_parent) }, .type = FlLayoutSizingType_Percent })
