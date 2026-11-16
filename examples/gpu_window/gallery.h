#pragma once

#include "scene.h"

typedef struct Gallery {
    Painter painter;
    FlImageHandle image;
    SvgIcon svg_mono;
    SvgIcon svg_color;
} Gallery;

void gallery_build(const Gallery* g, FlRenderFrame* frame);
