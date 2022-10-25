#pragma once

#include "types.h"

struct FlContext;
struct FlStyle;
struct FlVertPosColor;
struct PrimitiveRect;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generate vertex/trianglelist for box primitive

void PrimitiveRect_generate_render_data(struct FlContext* ctx, const struct PrimitiveRect* primitive);
