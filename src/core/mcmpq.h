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

// The queue is fixed-capacity (SLOTS entries); both operations are non-blocking and report
// full/empty via the return value. There is no blocking variant: callers own the policy for a
// saturated queue.
static inline bool try_enqueue(queue_t* queue, const void* item) {
    size_t head = atomic_load(&queue->head);
    for (;;) {
        slot_t* slot = &queue->slots[head % SLOTS];
        if ((head / SLOTS) * 2 == atomic_load(&slot->turn)) {
            size_t expected_head = head;
            if (atomic_compare_exchange_strong(&queue->head, &expected_head, head + 1)) {
                memcpy(slot->data, item, SLOT);
                atomic_store(&slot->turn, (head / SLOTS) * 2 + 1);
                return true;
            }
            head = expected_head;
        } else {
            size_t prev_head = head;
            head = atomic_load(&queue->head);
            if (head == prev_head) {
                return false;
            }
        }
    }
}

static inline bool try_dequeue(queue_t* queue, void* item) {
    size_t tail = atomic_load(&queue->tail);
    for (;;) {
        slot_t* slot = &queue->slots[tail % SLOTS];
        if ((tail / SLOTS) * 2 + 1 == atomic_load(&slot->turn)) {
            size_t expected_tail = tail;
            if (atomic_compare_exchange_strong(&queue->tail, &expected_tail, tail + 1)) {
                memcpy(item, slot->data, SLOT);
                atomic_store(&slot->turn, (tail / SLOTS) * 2 + 2);
                return true;
            }
            tail = expected_tail;
        } else {
            size_t prev_tail = tail;
            tail = atomic_load(&queue->tail);
            if (tail == prev_tail) {
                return false;
            }
        }
    }
}
