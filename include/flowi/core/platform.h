#pragma once

// Public platform, compiler, architecture, and attribute detection macros.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Platform detection

#if defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS 1
#define PLATFORM_MACOS 0
#define PLATFORM_LINUX 0
#elif defined(__APPLE__) && defined(__MACH__)
#define PLATFORM_WINDOWS 0
#define PLATFORM_MACOS 1
#define PLATFORM_LINUX 0
#elif defined(__linux__)
#define PLATFORM_WINDOWS 0
#define PLATFORM_MACOS 0
#define PLATFORM_LINUX 1
#else
#define PLATFORM_WINDOWS 0
#define PLATFORM_MACOS 0
#define PLATFORM_LINUX 0
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compiler detection
//
// Pure MSVC compilation is not supported. Windows builds use clang-cl, which
// defines both __clang__ and _MSC_VER and therefore enables both Clang and
// MSVC-compatible API detection.

#if defined(_MSC_VER) && !defined(__clang__)
#error "MSVC is not supported. Use clang-cl with Visual Studio or Clang on other platforms."
#endif

#if defined(__clang__)
#define COMPILER_CLANG 1
#else
#define COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define COMPILER_GCC 1
#else
#define COMPILER_GCC 0
#endif

#if defined(_MSC_VER)
#define COMPILER_MSVC 1
#else
#define COMPILER_MSVC 0
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CPU architecture detection

#if defined(__x86_64__) || defined(_M_X64)
#define CPU_X64 1
#define CPU_X86 0
#define CPU_ARM64 0
#define CPU_ARM32 0
#elif defined(__i386__) || defined(_M_IX86)
#define CPU_X64 0
#define CPU_X86 1
#define CPU_ARM64 0
#define CPU_ARM32 0
#elif defined(__aarch64__) || defined(_M_ARM64)
#define CPU_X64 0
#define CPU_X86 0
#define CPU_ARM64 1
#define CPU_ARM32 0
#elif defined(__arm__) || defined(_M_ARM)
#define CPU_X64 0
#define CPU_X86 0
#define CPU_ARM64 0
#define CPU_ARM32 1
#else
#define CPU_X64 0
#define CPU_X86 0
#define CPU_ARM64 0
#define CPU_ARM32 0
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if COMPILER_CLANG || COMPILER_GCC
#define UNUSED(x) ((void)(x))
#elif COMPILER_MSVC
#define UNUSED(x) __pragma(warning(suppress : 4100))(x)
#else
#define UNUSED(x) ((void)(x))
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if COMPILER_MSVC
#define ALWAYS_INLINE __forceinline
#elif COMPILER_CLANG || COMPILER_GCC
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define ALWAYS_INLINE inline
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if COMPILER_MSVC
#define NO_INLINE __declspec(noinline)
#elif COMPILER_CLANG || COMPILER_GCC
#define NO_INLINE __attribute__((noinline))
#else
#define NO_INLINE
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A function that never comes back. Telling the compiler lets it drop the code after the call - and lets a static
// analyser stop exploring the path that reached it.

#if COMPILER_MSVC
#define NORETURN __declspec(noreturn)
#elif COMPILER_CLANG || COMPILER_GCC
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if COMPILER_CLANG || COMPILER_GCC
#define UNUSED_FUNCTION __attribute__((unused))
#elif COMPILER_MSVC
#define UNUSED_FUNCTION
#else
#define UNUSED_FUNCTION
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// For a variable a header defines for its macros to use: every translation unit that includes the header gets one,
// but only the ones that expand a macro touch it, and GCC reports the rest.

#if COMPILER_CLANG || COMPILER_GCC
#define UNUSED_VARIABLE __attribute__((unused))
#elif COMPILER_MSVC
#define UNUSED_VARIABLE
#else
#define UNUSED_VARIABLE
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Branch prediction hints

#if COMPILER_CLANG || COMPILER_GCC
#define FL_EXPECT(expr, val) __builtin_expect((expr), (val))
#else
#define FL_EXPECT(expr, val) (expr)
#endif

#define FL_LIKELY(expr) FL_EXPECT(expr, 1)
#define FL_UNLIKELY(expr) FL_EXPECT(expr, 0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Static analyzer hint for pointers that cannot be null in practice.

#if COMPILER_GCC
#define ANALYZER_ASSUME_NONNULL(ptr) \
    do {                             \
        if (!(ptr))                  \
            __builtin_unreachable(); \
    } while (0)
#else
#define ANALYZER_ASSUME_NONNULL(ptr) ((void)0)
#endif
