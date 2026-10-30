#pragma once

// The ABI probes simulate an external consumer that only has flowi's public
// headers, so this tests raw _WIN32 instead of core.h's PLATFORM_WINDOWS.
#if defined(_WIN32)
#define FLOWI_TEST_EXPORT __declspec(dllexport)
#else
#define FLOWI_TEST_EXPORT __attribute__((visibility("default")))
#endif
