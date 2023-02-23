#pragma once

#if defined(__AVX__) || defined(__AVX2__)
#	include <immintrin.h>
#	undef  FLI_SIMD_AVX
#	define FLI_SIMD_AVX 1
#endif //

#if defined(__SSE2__) || defined(_MSC_VER)
#	include <emmintrin.h> // __m128i
#	if defined(__SSE4_1__)
#		include <smmintrin.h>
#	endif // defined(__SSE4_1__)
#	include <xmmintrin.h> // __m128
#	undef  FLI_SIMD_SSE
#	define FLI_SIMD_SSE 1
#elif defined(__ARM_NEON__)
#	include <arm_neon.h>
#	undef  FLI_SIMD_NEON
#	define FLI_SIMD_NEON 1
#endif //

#if defined(FLI_SIMD_SSE)
#include "simd_sse.inl"
#elif defined(FLI_SIMD_NEON)
#include "simd_neon.h"
#else
#pragma error("missing ref implementation")
#endif

