#pragma once

#ifndef FLI_INDEX_SIZE
#define FLI_INDEX_SIZE 2
#endif

#if FLI_INDEX_SIZE == 2
typedef uint16_t FlIdxSize;
#elif FLI_INDEX_SIZE == 4
typedef uint32_t FlIdxSize;
#else
#error "Unsupported index size. Only u16 or u32 supported"
#endif

