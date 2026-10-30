// Draw-kind and per-kind flag/effect names, shared by the classification and raster passes. The values mirror
// the C side one-for-one: the kinds are GpuDrawKind, the flag bits GPU_DRAW_IMAGE_* / GPU_DRAW_ARC_* /
// GPU_DRAW_POLY_*, the effect fields GPU_DRAW_EFFECT_* (all in gpu_ir_to_draw_cmd.h).
#define KIND_RECT 0u       // GpuDraw_Rect
#define KIND_IMAGE 1u      // GpuDraw_Image (atlas)
#define KIND_ARC 2u        // GpuDraw_Arc
#define KIND_CIRCLE 3u     // GpuDraw_Circle
#define KIND_DIAMOND 4u    // GpuDraw_Diamond
#define KIND_BORDER 5u     // GpuDraw_Border
#define KIND_SLOT_IMAGE 6u // GpuDraw_SlotImage
#define KIND_POLYLINE 7u   // GpuDraw_Polyline
#define KIND_GLYPH 8u      // GpuDraw_Glyph (R8 coverage atlas, colorized by the run tint)
#define KIND_SVG 9u        // GpuDraw_Svg (R8 SVG coverage atlas, colorized by the icon tint)
#define KIND_SVG_COLOR 10u // GpuDraw_SvgColor (RGBA8 SVG atlas, baked premultiplied color x white tint)
#define IMAGE_NEAREST 1u   // GPU_DRAW_IMAGE_NEAREST (atlas/slot: nearest filter)
#define ARC_ROUND_CAPS 1u  // GPU_DRAW_ARC_ROUND_CAPS
#define ARC_FULL 2u        // GPU_DRAW_ARC_FULL (sweep is the full circle: skip the wedge cut)
#define POLY_STROKE 1u     // GPU_DRAW_POLY_STROKE (clear = fill trapezoid)
#define EFFECT_SHIFT 8u    // GPU_DRAW_EFFECT_SHIFT: effect kind in flags bits 8..9
#define EFFECT_MASK 3u     // GPU_DRAW_EFFECT_MASK, shifted down
#define EFFECT_SHADOW 1u   // drop shadow
#define EFFECT_GLOW 2u     // glow
#define DASH_SHIFT 10u     // GPU_DRAW_DASH_SHIFT: Border/Circle dash length in flags bits 10..20
#define DASH_MASK 2047u    // GPU_DRAW_DASH_MASK (11 bits)
#define GAP_SHIFT 21u      // GPU_DRAW_GAP_SHIFT: gap length in flags bits 21..31
#define DASH_PX_PER_UNIT 0.0625 // 1 / GPU_DRAW_DASH_UNITS_PER_PX
