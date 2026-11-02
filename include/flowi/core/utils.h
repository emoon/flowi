#pragma once

// Bare-name convenience macros for implementation code. The spellings (min, max, clamp,
// for_count) are unprefixed and would collide in a consumer's namespace, so this header is
// explicitly opt-in and must never be included by another public flowi header.

#include <stdint.h>

#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)

// Strips the const off a type so a counter derived from a const bound stays assignable.
#define REMOVE_CONST(T) __typeof__((T) + 0)

// Iterates i over [0, n), evaluating n exactly once.
#define for_count(i, n) for (REMOVE_CONST(n) i = 0, CONCAT(_count_, __LINE__) = (n); i < CONCAT(_count_, __LINE__); ++i)

#define sizeof_array(arr) (sizeof(arr) / sizeof((arr)[0]))

// Byte-size literals, widened to 64 bits so a gigabyte count cannot overflow int.
#define KB(x) ((uint64_t)(x) * 1024ULL)
#define MB(x) ((uint64_t)(x) * 1024ULL * 1024ULL)
#define GB(x) ((uint64_t)(x) * 1024ULL * 1024ULL * 1024ULL)

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Math utility macros
//
// These evaluate their arguments more than once, so pass side-effect-free expressions. Each keeps the
// argument's own type.
//
// A function-like macro of the same name breaks <algorithm>'s std::min/std::max/std::clamp declarations,
// so C++ consumers get the standard ones instead.

#ifndef __cplusplus

// Windows headers define min/max as macros; ours win.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define clamp(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

#endif
