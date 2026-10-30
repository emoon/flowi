// The draw-command struct, shared by the raster and classification shaders. std430 layout, 80-byte stride;
// field meaning per kind is documented on GpuDrawCmd in gpu_ir_to_draw_cmd.h.
struct DrawCmd {
    float4 rect;
    float4 radius;
    uint4 color;
    uint kind;
    uint clip; // index into ClipRects
    uint flags;
    uint aux;
    uint4 effect;
};
