#pragma once

// GPU backend: packing of IR values into GpuDrawCmd words.

#include "gpu_ir_to_draw_cmd.h"
#include <ui/render_ir.h>

// Border side widths and effect offsets/blur ride the packed u32 words bit-verbatim; the shader reads them back
// with uintBitsToFloat.
u32 gpu_f32_bits(f32 v);

// Pack an FlColor with the frame's global_alpha folded into the alpha channel. Byte order matches GLSL
// unpackUnorm4x8: R in the low byte. The alpha product rounds to nearest so 255 * 255 stays 255.
u32 gpu_pack_color(FlColor c, u8 global_alpha);

// Image tints pack premultiplied (rgb * a) so the shader's modulation of the premultiplied atlas texel is a
// single componentwise multiply. The opaque white tint stays the exact identity.
u32 gpu_pack_tint_premultiplied(FlColor tint, u8 global_alpha);

// Pack a dash pattern into the flags word's free high bits (format in GPU_DRAW_DASH_*). Either length
// non-positive means solid, as does a pattern whose dash or gap rounds away to nothing at the 1/16px step.
u32 gpu_pack_dash(FlStrokeStyle stroke);

// Stamp an effect onto a committed command: the kind into the flags' effect field, the offset/blur as f32 bits
// and the alpha-folded color into the effect words.
void gpu_pack_effect(GpuDrawCmd* cmd, const FlEffect* e, u8 global_alpha);
