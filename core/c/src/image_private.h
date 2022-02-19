#pragma once

#include <flowi_core/image.h>
#include <flowi_core/render_commands.h>
#include "types.h"

struct Atlas;
struct FlContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ImagePrivate {
    u64 handle;
    char* name;
    u8* data;
    FlImageInfo info;
    FlTextureFormat format;
} ImagePrivate;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Image_add_to_rect(ImagePrivate* self, struct FlContext* ctx, struct Atlas* atlas);