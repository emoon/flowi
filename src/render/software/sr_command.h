#pragma once

#include <core/math.h>
#include <core/simd.h>
#include <ui/image.h>

#define RenderMask_Corners_Shift 17

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlArena;
struct GradientQuadState;
struct ImageData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum RenderMask {
    RenderMask_None = 0,
    // Only one of these bits can be used at once, the code will not check for any other masks when these are set
    RenderMask_ClipRectBegin = 1 << 2,
    RenderMask_ClipRectEnd = 1 << 3,
    RenderMask_DrawQuad = 1 << 4,
    RenderMask_BlendWithBackground = 1 << 5,
    RenderMask_ColorGradient = 1 << 6,
    RenderMask_TextureSameSize = 1 << 7,
    // Texture isn't the same size as the quad, so the scaled path is needed
    RenderMask_TextureScaled = 1 << 8,
    RenderMask_Text = 1 << 9,
    // Border rendering with dual radii (outer and inner)
    RenderMask_Border = 1 << 10,
    RenderMask_Corners = 1 << 16, // Set if any corner is active
} RenderMask;

// Which corner a RenderMask_Corners command covers. This is a value, not a bit: it is packed into the field at
// RenderMask_Corners_Shift, so mask & <corner> matches unrelated corners and must never be used. Pack with
// render_mask_corner(), read back with render_mask_corner_of().
typedef enum RenderCorner {
    RenderCorner_None = 0,
    RenderCorner_TopLeft = 1,
    RenderCorner_TopRight = 2,
    RenderCorner_BottomLeft = 3,
    RenderCorner_BottomRight = 4,
} RenderCorner;

#define render_mask_corner(corner) ((u32)(corner) << RenderMask_Corners_Shift)
#define render_mask_corner_of(mask) ((RenderCorner)((u32)(mask) >> RenderMask_Corners_Shift))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct RenderText {
    f32x4 aabb;
    u8x16 color;
    const u8* data_r;
    const u8* data_g;
    const u8* data_b;
    int stride;
} RenderText;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ScaledTexturePrecomputed {
    u16* load_offsets;      // Per-destination-pixel: load offset for 8-pixel group starting here
    u8x16* shuffle_masks;   // Per-destination-pixel: shuffle mask for 8-pixel group starting here
    const u16* src_indices; // Per-destination-pixel: source column index (scalar-gather fallback path)
    int dst_width;
    bool wide_spread; // 8-pixel source spread can exceed the 32-byte shuffle window (downscale > ~4.4x)
} ScaledTexturePrecomputed;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct RenderCommand {
    RenderMask mask;
    u8 global_alpha; // Global alpha for fade transitions (0-255)

    union {
        struct {
            f32x4 aabb;
            u8x16 corner_colors;
            i16x8 clip_adjust;
            struct GradientQuadState* gradient_data;
            ScaledTexturePrecomputed* scaled_texture;
            struct ImageData* image;
            // Source pixel offset for same-size image sub-rect sampling; zero for full-image draws.
            int image_src_x;
            int image_src_y;
            float radius;
            float inner_radius; // For border rendering (border_width = radius - inner_radius)
        };

        RenderText render_text;
    };
} RenderCommand;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sr_command_setup(struct FlArena* arena, RenderCommand* command);
int sr_command_split_rounded(struct FlArena* arena, RenderCommand* initial_command, f32x4 corners);
