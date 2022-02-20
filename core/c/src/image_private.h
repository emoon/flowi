#pragma once

#include <flowi_core/image.h>
#include <flowi_core/render_commands.h>
#include "types.h"

struct Atlas;
struct FlContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ImagePrivate {
    u64 handle;
    u8* data;
    FlString name;
    u32 texture_id;
    int atlas_x;
    int atlas_y;
    FlImageInfo info;
    FlTextureFormat format;
} ImagePrivate;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Image_add_to_atlas(const u8* cmd, struct Atlas* atlas);
bool Image_render(struct FlContext* ctx, const u8* cmd);