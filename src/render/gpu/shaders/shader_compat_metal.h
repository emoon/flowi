// Metal Shading Language expansions of the shared shader vocabulary - the MSL sibling of
// shader_compat_glsl.h.
//
// This is NOT a C header - never #include it from C. It is embedded as a NUL-terminated byte array by the build
// (cmake, symbol gpu_glsl_shader_compat_metal) and spliced into every compute program's source-string list by
// gpu_shaders.c, right after the #include <metal_stdlib> dialect preamble. On Metal the vector types are already
// the native MSL spellings, so this header defines no type aliases; it reconciles the builtins GLSL spells
// differently and marks the per-backend signature seam.
//
// Two seams stay per-backend. The entry point and its resource
// binding: MSL passes every buffer/image as a [[buffer(N)]]/[[texture(N)]] function argument and the thread index
// as [[thread_position_in_grid]], so each shader carries a small #ifdef FL_METAL block around JUST its signature,
// selected by the FL_METAL this header defines. And the combined-sampler paths: every sampled kind declares its
// textures as kernel arguments and its samplers function-locally inside that block, so this header defines none
// of the SAMPLER2D global-scope vocabulary the GL header spells.

#define FL_METAL 1

// --- gl_* builtin names -> the MSL kernel-argument the signature binds -----------------------------------
// The shader bodies keep the GLSL builtin spelling for the thread index. Each shader's signature block declares
// matching [[thread_position_in_grid]] / [[threadgroup_position_in_grid]] / [[thread_index_in_threadgroup]]
// arguments under these names - uint3 for the two positions, so .xy selects the 2D index; uint for the flat one.
#define gl_GlobalInvocationID _fl_global_id
#define gl_WorkGroupID _fl_group_id
#define gl_LocalInvocationIndex _fl_local_index

// --- builtins GLSL spells differently --------------------------------------------------------------------
// textureLod / unpackUnorm4x8 / uintBitsToFloat are the GLSL builtins with no vocabulary name of their own,
// reconciled here against their MSL equivalents.
#define uintBitsToFloat(x) as_type<float>(x)
#define unpackUnorm4x8(x) unpack_unorm4x8_to_float(x)

// mod(x, y): GLSL's floored modulo - the result takes the sign of the divisor, so mod of a negative value stays
// in [0, y). MSL provides only fmod, which truncates toward zero and returns a negative remainder, so it is NOT a
// drop-in. Scalar float only.
//
// Contract: both arguments are evaluated more than once, so they must be side-effect free and cheap. GLSL's
// native mod evaluates each once, so a body that violated this would diverge between backends rather than fail
// to compile.
#define mod(x, y) ((x) - (y) * floor((x) / (y)))

// atan(y, x): GLSL spells the two-argument arc tangent as an overload of atan; MSL spells it atan2 and keeps
// atan strictly one-argument. Only the two-argument form exists in the shared bodies - a one-argument atan would
// not preprocess through this.
#define atan(y, x) atan2(y, x)

// textureLod(tex, uv, lod): sample a texture at an explicit LOD. GLSL's sampler2D bundles the sampler state into
// the texture handle; MSL splits them, so the sample call needs a sampler object. This macro serves the
// linear-filtered kinds through _fl_sampler_linear, which each sampling kernel declares as a function-local
// constexpr sampler inside its signature block. The nearest-filtered paths take a second function-local sampler
// the raster body reaches directly under a #ifdef FL_METAL fork, not through this macro.
#define textureLod(tex, uv, lod) tex.sample(_fl_sampler_linear, uv, level(lod))

// --- shared memory, sync, atomics, image store -----------------------------------------------------------
// BARRIER is the threadgroup execution+memory barrier. WRITE_IMAGE stores one texel to a write-access texture.
// SHARED declares threadgroup memory; on
// Metal a threadgroup array is a function-local declaration, so each kernel places it inside its signature block
// rather than at global scope.
//
// WRITE_IMAGE contract: coord is a signed int2 in the vocabulary, but MSL's texture write takes an unsigned
// coordinate, so this reinterprets it. Callers must pass a never-negative coordinate - every store site derives
// one from an unsigned cell origin plus a thread index, and the shared bodies bounds-check the ragged edge before
// storing.
#define BARRIER() threadgroup_barrier(mem_flags::mem_threadgroup)
#define WRITE_IMAGE(img, coord, val) img.write(val, uint2(coord))
#define SHARED(T, name, N) threadgroup T name[N]
