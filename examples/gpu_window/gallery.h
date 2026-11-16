#pragma once

#include "scene.h"

typedef struct Gallery {
    Painter painter;
    FlImageHandle image;
    SvgIcon svg_mono;
    SvgIcon svg_color;
} Gallery;

// Builds the gallery scene into the rewound IR arena and points frame at the recorded commands.
void gallery_build(const Gallery* g, FlRenderFrame* frame);
