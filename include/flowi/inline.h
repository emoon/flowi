#pragma once

#if defined(__GNUC__)
#define FL_INLINE static inline
#elif defined(_MSC_VER)
#define FL_INLINE __forceinline static
#else
#error "Unsupported compiler"
#endif

