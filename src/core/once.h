#pragma once

#include "types.h"
#include "os/os.h"
#include <stdatomic.h>

typedef _Atomic u32 OnceFlag;

enum {
    OnceState_Uninitialized = 0,
    OnceState_Running = 1,
    OnceState_Done = 2,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Returns true to exactly one caller, which must run the initialisation and then call once_end.
// Every other caller blocks until that initialisation has finished and gets false.
static inline bool once_begin(OnceFlag* flag) {
    u32 expected = OnceState_Uninitialized;
    if (atomic_compare_exchange_strong_explicit(flag, &expected, OnceState_Running, memory_order_acquire,
                                                memory_order_acquire)) {
        return true;
    }

    while (atomic_load_explicit(flag, memory_order_acquire) != OnceState_Done) {
        sleep_us(50);
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void once_end(OnceFlag* flag) {
    atomic_store_explicit(flag, OnceState_Done, memory_order_release);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline bool once_is_done(OnceFlag* flag) {
    return atomic_load_explicit(flag, memory_order_acquire) == OnceState_Done;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void once_reset(OnceFlag* flag) {
    atomic_store_explicit(flag, OnceState_Uninitialized, memory_order_release);
}
