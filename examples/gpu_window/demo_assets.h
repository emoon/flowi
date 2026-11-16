#pragma once

#include "scene.h"

typedef struct SvgSource {
    const u8* data;
    size_t size;
    const char* name;
} SvgSource;

#define SVG_SOURCE(bytes, file_name) ((SvgSource) { (bytes), sizeof(bytes), (file_name) })

void prepare_demo_image(FlRenderBackend* backend, FlArena* scratch, FlImageHandle handle);

SvgIcon load_svg_icon(FlArena* arena, SvgSource source, FlImageHandle handle);
