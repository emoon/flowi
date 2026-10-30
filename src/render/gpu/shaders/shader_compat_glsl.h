// GL 4.3-core expansions of the shared shader vocabulary.
//
// This is NOT a C header - never #include it from C. It is embedded as a NUL-terminated byte array by the build
// (cmake/embed_glsl.cmake, symbol gpu_glsl_shader_compat_glsl) and spliced into every compute program's
// source-string list by gpu_shaders.c, right after the dialect preamble. Each *.glsl body is written against the
// vocabulary defined here, never a backend-native spelling; shader_compat_metal.h defines the same names against
// MSL. Two seams stay per-backend: the entry-point + resource-binding site, and the GLSL builtins with no
// vocabulary name of their own (textureLod, unpackUnorm4x8, uintBitsToFloat), which the Metal header reconciles.
//
// The expansions below are the raw GLSL spellings verbatim, so after preprocessing each compute program is
// byte-identical to the same shader written in plain GLSL.

// --- scalar/vector type aliases -------------------------------------------------------------------------
// The vocabulary spells vector types the MSL way (float4, uint2, ...), so the raw GLSL keyword (vec4, uvec2, ...)
// appears only as an expansion here, never in a shader body. Scalars spell the same in both dialects.
#define float2 vec2
#define float4 vec4
#define int2 ivec2
#define uint2 uvec2
#define uint3 uvec3
#define uint4 uvec4

// --- kernel entry + dispatch shape ----------------------------------------------------------------------
// The entry point and its resource binding are the per-backend seam left un-bridged: on GL the kernel is a plain
// void main(), the workgroup shape a layout(local_size_*) in; statement, and resources are global-scope
// declarations. All shaders dispatch a 2D grid.
#define KERNEL(name) void name()
#define LOCAL_SIZE_2D(x, y) layout(local_size_x = x, local_size_y = y) in

// --- resource declarations (the GL side of the binding seam) --------------------------------------------
// BUFFER/WRITE_BUFFER declare an SSBO of T[] at binding N; block is the GLSL interface-block tag (unique per
// shader, matched only within the program - GL binds by index, and Metal has no block name). IMAGE2D_WRITE
// declares a write-only image2D of layout format fmt. UNIFORM/UNIFORM_ARRAY/SAMPLER2D* declare program
// uniforms. Each expands without a trailing ;, so the shader body supplies it.
#define BUFFER(block, T, name, N) layout(std430, binding = N) readonly buffer block { T name[]; }
#define WRITE_BUFFER(block, T, name, N) layout(std430, binding = N) writeonly buffer block { T name[]; }
#define IMAGE2D_WRITE(fmt, name, N) layout(fmt, binding = N) uniform writeonly image2D name
#define UNIFORM(T, name) uniform T name
#define UNIFORM_ARRAY(T, name, N) uniform T name[N]
#define SAMPLER2D(name) uniform sampler2D name
#define SAMPLER2D_ARRAY(name, N) uniform sampler2D name[N]

// --- shared memory, sync, atomics, image store ----------------------------------------------------------
// SHARED declares a threadgroup/workgroup-shared array. BARRIER is the execution+memory barrier. WRITE_IMAGE stores one texel to a write-only image.
//
// WRITE_IMAGE contract: coord is a signed int2, which imageStore takes natively here - but the Metal twin
// reinterprets it as unsigned, so a negative coordinate would diverge between backends. Every store site must
// pass a never-negative coordinate (each derives one from an unsigned cell origin plus a thread index, with the
// ragged edge bounds-checked before the store), and the contract holds on both sides of the vocabulary.
#define SHARED(T, name, N) shared T name[N]
#define BARRIER() barrier()
#define WRITE_IMAGE(img, coord, val) imageStore(img, coord, val)
