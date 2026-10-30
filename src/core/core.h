#pragma once

#include "types.h"
#include <flowi/arena/arena.h> // @generated fl_init / fl_destroy (api_gen)
#include <flowi/core/platform.h>
#include <flowi/core/utils.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DLL export/import macros

#if PLATFORM_WINDOWS
#ifdef BASE_STATIC
#define DLL_EXPORT // Static library - no import/export
#elif defined(BASE_EXPORTS)
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT __declspec(dllimport)
#endif
#else
#define DLL_EXPORT __attribute__((visibility("default")))
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define KB(x) ((u64)(x) * 1024ULL)
#define MB(x) ((u64)(x) * 1024ULL * 1024ULL)
#define GB(x) ((u64)(x) * 1024ULL * 1024ULL * 1024ULL)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if COMPILER_MSVC
#define align_of(type) __alignof(type)
#elif COMPILER_CLANG || COMPILER_GCC
#define align_of(type) __alignof__(type)
#else
#if __STDC_VERSION__ >= 201112L
#define align_of(type) _Alignof(type)
#else
#define align_of(type)   \
    ((size_t)&((struct { \
         char c;         \
         type t;         \
     }*)0)               \
         ->t)
#endif
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define thread_local _Thread_local
#else
#if COMPILER_MSVC
#define thread_local __declspec(thread)
#elif COMPILER_GCC || COMPILER_CLANG
#define thread_local __thread
#else
#define thread_local
#warning "THREAD_LOCAL not defined for this compiler; thread-local storage may not be supported."
#endif
#endif

#if COMPILER_MSVC
#define CACHE_ALIGNED __declspec(align(64))
#elif COMPILER_CLANG || COMPILER_GCC
#define CACHE_ALIGNED __attribute__((aligned(64)))
#else
#define CACHE_ALIGNED
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//- Doubly-linked lists

#define dll_insert_npz(nil, f, l, p, n, next, prev)                                                    \
    (((f) == nullptr)   ? ((f) = (l) = (n), (n)->next = nullptr, (n)->prev = nullptr)                  \
     : ((p) == nullptr) ? ((n)->next = (f), (f)->prev = (n), (f) = (n), (n)->prev = nullptr)           \
     : ((p) == (l))     ? ((l)->next = (n), (n)->prev = (l), (l) = (n), (n)->next = nullptr)           \
                        : ((((p) != nullptr && (p)->next != nullptr) ? ((p)->next->prev = (n)) : (0)), \
                           ((n)->next = (p)->next), ((p)->next = (n)), ((n)->prev = (p))))

#define dll_push_back_npz(nil, f, l, n, next, prev) dll_insert_npz(nil, f, l, l, n, next, prev)
#define dll_push_front_npz(nil, f, l, n, next, prev) dll_insert_npz(nil, l, f, f, n, prev, next)

#define dll_remove_npz(nil, f, l, n, next, prev)                                 \
    (((n) == (f) ? (f) = (n)->next : (0)), ((n) == (l) ? (l) = (l)->prev : (0)), \
     (((n)->prev == nullptr) ? (0) : ((n)->prev->next = (n)->next)),             \
     (((n)->next == nullptr) ? (0) : ((n)->next->prev = (n)->prev)))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//- Singly-linked, doubly-headed lists (queues)

#define sll_queue_push_nz(nil, f, l, n, next) \
    (((f) == nullptr) ? ((f) = (l) = (n), (n)->next = nullptr) : ((l)->next = (n), (l) = (n), (n)->next = nullptr))

#define sll_queue_push_front_nz(nil, f, l, n, next) \
    (((f) == nullptr) ? ((f) = (l) = (n), (n)->next = nullptr) : ((n)->next = (f), (f) = (n)))

#define sll_queue_pop_nz(nil, f, l, next) (((f) == (l)) ? ((f) = nullptr, (l) = nullptr) : ((f) = (f)->next))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//- Singly-linked, singly-headed lists (stacks)

#define sll_stack_push_n(f, n, next) ((n)->next = (f), (f) = (n))
#define sll_stack_pop_n(f, next) ((f) = (f)->next)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//- Doubly-linked list helpers (using nullptr as default nil value)

#define dll_push_back_np(f, l, n, next, prev) dll_push_back_npz(nullptr, f, l, n, next, prev)
#define dll_insert(f, l, p, n) dll_insert_npz(nullptr, f, l, p, n, next, prev)
#define dll_push_back(f, l, n) dll_push_back_npz(nullptr, f, l, n, next, prev)
#define dll_push_front(f, l, n) dll_push_front_npz(nullptr, f, l, n, next, prev)
#define dll_remove(f, l, n) dll_remove_npz(nullptr, f, l, n, next, prev)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//- Singly-linked, doubly-headed list helpers (using nullptr as default nil value)

#define sll_queue_push(f, l, n) sll_queue_push_nz(nullptr, f, l, n, next)
#define sll_queue_push_front(f, l, n) sll_queue_push_front_nz(nullptr, f, l, n, next)
#define sll_queue_pop(f, l) sll_queue_pop_nz(nullptr, f, l, next)
#define sll_stack_push(f, n) sll_stack_push_n(f, n, next)
#define sll_stack_pop(f) sll_stack_pop_n(f, next)

#define sll_queue_remove(f, l, n)              \
    do {                                       \
        if ((f) == (n)) {                      \
            sll_queue_pop(f, l);               \
        } else {                               \
            __typeof__(f) _prev = (f);         \
            for_each_list(_curr, f) {          \
                if (_curr == (n)) {            \
                    _prev->next = _curr->next; \
                    if ((l) == (n)) {          \
                        (l) = _prev;           \
                    }                          \
                    break;                     \
                }                              \
                _prev = _curr;                 \
            }                                  \
        }                                      \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define for_each_list(node, front) for (REMOVE_CONST(front) node = (front); node != nullptr; node = node->next)
