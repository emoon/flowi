#pragma once

#include "core.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define SLOTS 8192
#define SLOT sizeof(void*)

typedef struct {
    CACHE_ALIGNED
    _Atomic size_t turn;
    unsigned char data[SLOT];
} slot_t;

typedef struct {
    CACHE_ALIGNED _Atomic size_t head;
    CACHE_ALIGNED _Atomic size_t tail;
    CACHE_ALIGNED slot_t slots[SLOTS];
} queue_t;

static inline bool claim_cursor(_Atomic size_t* counter, size_t* cursor) {
    size_t expected = *cursor;
    if (atomic_compare_exchange_strong(counter, &expected, *cursor + 1)) {
        return true;
    }

    *cursor = expected;
    return false;
}

// Reloads the cursor after a stale slot; false when it did not move (queue full/empty).
static inline bool reload_cursor(_Atomic size_t* counter, size_t* cursor) {
    size_t prev = *cursor;
    *cursor = atomic_load(counter);
    return *cursor != prev;
}

// The queue is fixed-capacity (SLOTS entries); both operations are non-blocking and report
// full/empty via the return value. There is no blocking variant: callers own the policy for a
// saturated queue.
static inline bool try_enqueue(queue_t* queue, const void* item) {
    size_t head = atomic_load(&queue->head);
    for (;;) {
        slot_t* slot = &queue->slots[head % SLOTS];
        if ((head / SLOTS) * 2 != atomic_load(&slot->turn)) {
            if (!reload_cursor(&queue->head, &head)) {
                return false;
            }
            continue;
        }

        if (claim_cursor(&queue->head, &head)) {
            memcpy(slot->data, item, SLOT);
            atomic_store(&slot->turn, (head / SLOTS) * 2 + 1);
            return true;
        }
    }
}

static inline bool try_dequeue(queue_t* queue, void* item) {
    size_t tail = atomic_load(&queue->tail);
    for (;;) {
        slot_t* slot = &queue->slots[tail % SLOTS];
        if ((tail / SLOTS) * 2 + 1 != atomic_load(&slot->turn)) {
            if (!reload_cursor(&queue->tail, &tail)) {
                return false;
            }
            continue;
        }

        if (claim_cursor(&queue->tail, &tail)) {
            memcpy(item, slot->data, SLOT);
            atomic_store(&slot->turn, (tail / SLOTS) * 2 + 2);
            return true;
        }
    }
}
