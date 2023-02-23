#pragma once

#include <flowi/image.h>
#include <flowi/render_commands.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Atlas;
struct FlContext;
struct NSVGimage;
struct NSVGrasterizer;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ImagePrivate {
    u64 handle;
    u8* data;
    struct NSVGimage* svg_image;
    struct NSVGrasterizer* svg_raster;
    FlString name;
    FlSvgFlags svg_flags;
    u32 texture_id;
    int atlas_x;
    int atlas_y;
    float u0,v0,u1,v1;
    FlImageInfo info;
    FlTextureFormat format;
} ImagePrivate;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Image_add_to_atlas(const u8* cmd, struct Atlas* atlas);
bool Image_render(struct FlContext* ctx, const u8* cmd);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

