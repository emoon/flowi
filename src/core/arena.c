#define ARENA_ALLOC_IMPL
#include "arena.h"
#include "arena_alloc_tracker.h"
#include "core.h"
#include "assert.h"
#include "log.h"
#include "math.h"
#include "memory.h"
#include "once.h"
#include "os/os.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

thread_local static FlArena* s_arena_scratch = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global Arena Tracker

typedef struct {
    Mutex mutex; // Protects list access
    FlArena* head;
    FlArena* tail;
    OnceFlag initialized;
} ArenaTracker;

static ArenaTracker g_arena_tracker = { 0 };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_tracker_init(void) {
    if (once_begin(&g_arena_tracker.initialized)) {
        mutex_init(&g_arena_tracker.mutex);
        g_arena_tracker.head = nullptr;
        g_arena_tracker.tail = nullptr;
        once_end(&g_arena_tracker.initialized);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_tracker_destroy(void) {
    if (once_is_done(&g_arena_tracker.initialized)) {
        mutex_destroy(&g_arena_tracker.mutex);
        g_arena_tracker.head = nullptr;
        g_arena_tracker.tail = nullptr;
        once_reset(&g_arena_tracker.initialized);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_tracker_register(FlArena* self) {
    if (FL_UNLIKELY(!once_is_done(&g_arena_tracker.initialized))) {
        arena_tracker_init();
    }

    mutex_lock(&g_arena_tracker.mutex);
    dll_push_back_np(g_arena_tracker.head, g_arena_tracker.tail, self, next_tracked, prev_tracked);
    mutex_unlock(&g_arena_tracker.mutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_tracker_unregister(FlArena* self) {
    if (FL_UNLIKELY(!once_is_done(&g_arena_tracker.initialized))) {
        return;
    }

    mutex_lock(&g_arena_tracker.mutex);

    // Read the arena's tracking pointers while holding the lock
    // to avoid data races with other threads updating neighbors
    FlArena* next = self->next_tracked;
    FlArena* prev = self->prev_tracked;

    if (self == g_arena_tracker.head) {
        g_arena_tracker.head = next;
    }
    if (self == g_arena_tracker.tail) {
        g_arena_tracker.tail = prev;
    }

    if (prev != nullptr) {
        prev->next_tracked = next;
    }
    if (next != nullptr) {
        next->prev_tracked = prev;
    }

    mutex_unlock(&g_arena_tracker.mutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_tracker_iterate_all(ArenaIteratorCallback callback, void* user_data) {
    if (!callback) {
        return;
    }

    if (FL_UNLIKELY(!once_is_done(&g_arena_tracker.initialized))) {
        return;
    }

    // Pre-warm this thread's scratch arenas: creating one inside a callback would re-enter
    // arena_tracker_register on the non-recursive mutex held below.
    arena_scratch_init();

    mutex_lock(&g_arena_tracker.mutex);
    for (FlArena* arena = g_arena_tracker.head; arena != nullptr; arena = arena->next_tracked) {
        callback(arena, user_data);
    }
    mutex_unlock(&g_arena_tracker.mutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_stats(FlArena* arena, FlArenaStats* out) {
    // Relaxed: the counters are read for reporting, and nothing downstream orders other memory
    // against them.
    out->site_file = arena->allocation_site_file ? arena->allocation_site_file : "unknown";
    out->pos = atomic_load_explicit(&arena->pos, memory_order_relaxed);
    out->committed = atomic_load_explicit(&arena->committed_size, memory_order_relaxed);
    out->reserved = arena->reserved_size;
    out->site_line = arena->allocation_site_line;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FlArena functions

void vm_range_rewind(FlArena* self);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void vm_range_rewind(FlArena* self) {
    // Reset to the rewind floor: just after the arena structure, plus any header
    // (e.g. an FlArenaMt wrapper) that must survive a full rewind.
    atomic_store_explicit(&self->pos, self->rewind_pos, memory_order_relaxed);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlArena* arena_new_(const ArenaSettings* settings) {
    u64 page_size = get_page_size_os();
    u64 reserved_size = align_pow2(settings->reserved_size, page_size);

    void* ptr;
    ArenaError result = reserve_range_os(reserved_size, &ptr);
    if (FL_UNLIKELY(result != ARENA_SUCCESS)) {
        log_fatal("Unable to reserve memory for arena: %s:%d", settings->allocation_site_file,
                  settings->allocation_site_line);
        fl_log_flush();
        exit(1);
    }

    u64 commit_size = align_pow2(ARENA_SIZE, page_size);
    result = commit_memory_os(ptr, commit_size);

    if (FL_UNLIKELY(result != ARENA_SUCCESS)) {
        log_fatal("Unable to commit memory for arena: %s:%d", settings->allocation_site_file,
                  settings->allocation_site_line);
        fl_log_flush();
        exit(1);
    }

    FlArena* self = (FlArena*)ptr;
    self->next = nullptr;
    self->ptr = ptr;
    self->reserved_size = reserved_size;
    atomic_store_explicit(&self->committed_size, commit_size, memory_order_relaxed);
    atomic_store_explicit(&self->pos, ARENA_SIZE, memory_order_relaxed);
    self->rewind_pos = ARENA_SIZE;
    self->page_size = page_size;
    self->allocation_site_file = settings->allocation_site_file;
    self->allocation_site_line = settings->allocation_site_line;

    self->next_tracked = nullptr;
    self->prev_tracked = nullptr;
    atomic_store_explicit(&self->peak_offset, ARENA_SIZE, memory_order_relaxed);
    self->owner_thread_id = (u32)os_thread_get_current_id();
    self->creation_timestamp_us = (u64)get_current_time_us_os();

    self->alloc_tracker = nullptr;

    arena_tracker_register(self);

    return self;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlArena* fl_arena_create(u64 reserved_size, const char* file, int line) {
    ArenaSettings settings = {
        .reserved_size = reserved_size,
        .allocation_site_file = file,
        .allocation_site_line = line,
    };
    return arena_new_(&settings);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* fl_arena_current_pos(FlArena* self) {
    u64 current_pos = atomic_load_explicit(&self->pos, memory_order_relaxed);
    return ((u8*)self->ptr) + current_pos;
}

void* fl_arena_aligned_pos(FlArena* self, u64 alignment) {
    u64 current_pos = atomic_load_explicit(&self->pos, memory_order_relaxed);
    const u64 aligned_pos = (current_pos + alignment - 1) & ~(alignment - 1);
    return ((u8*)self->ptr) + aligned_pos;
}

FlTempArena fl_arena_temp_begin(FlArena* self) {
    return (FlTempArena) {
        .arena = self,
        .pos = atomic_load_explicit(&self->pos, memory_order_relaxed),
    };
}

void fl_arena_temp_end(FlTempArena temp) {
    atomic_store_explicit(&temp.arena->pos, temp.pos, memory_order_relaxed);
}

void fl_arena_scratch_end(const FlTempArena temp_arena) {
    atomic_store_explicit(&temp_arena.arena->pos, temp_arena.pos, memory_order_relaxed);
}

void fl_arena_compact_bytes(FlArena* self, u64 bytes_to_release) {
    FL_ASSERT(self != nullptr);
    u64 current_pos = atomic_load_explicit(&self->pos, memory_order_relaxed);
    // Releasing more than the arena holds would wrap pos past the end of the reservation.
    FL_ASSERT_FATAL(bytes_to_release <= current_pos);
    atomic_store_explicit(&self->pos, current_pos - bytes_to_release, memory_order_relaxed);
}

u64 fl_arena_count_elements(const FlArena* self, const void* start_ptr, u64 element_size) {
    FL_ASSERT(self != nullptr);
    FL_ASSERT(start_ptr != nullptr);
    FL_ASSERT(element_size > 0);

    const u8* arena_start = (const u8*)self->ptr;
    u64 current_pos = atomic_load_explicit(((_Atomic u64*)&self->pos), memory_order_relaxed);
    const u8* arena_current = arena_start + current_pos;
    const u8* start = (const u8*)start_ptr;

    FL_ASSERT(start >= arena_start && start <= arena_current);

    u64 bytes_used = (u64)(arena_current - start);
    return bytes_used / element_size;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Thread-safe (CAS-based) allocation - this is the default

void* fl_arena_alloc_raw(FlArena* self, u64 size, u64 alignment) {
    while (true) {
        u64 current_pos = atomic_load(&self->pos);
        const u64 aligned_pos = align_pow2(current_pos, alignment);

        // A size large enough to wrap aligned_pos + size would produce a small new_pos that slips
        // past the required_committed > reserved_size check below and hand back an overlapping pointer.
        if (FL_UNLIKELY(size > UINT64_MAX - aligned_pos)) {
            log_fatal("Out of reserved memory in arena (allocation size overflow): %s:%d", self->allocation_site_file,
                      self->allocation_site_line);
            fl_log_flush();
            exit(1);
        }

        const u64 new_pos = aligned_pos + size;

        if (atomic_compare_exchange_weak(&self->pos, &current_pos, new_pos)) {
            while (true) {
                u64 current_committed = atomic_load(&self->committed_size);
                if (new_pos <= current_committed) {
                    void* return_ptr = (char*)self->ptr + aligned_pos;

                    u64 current_peak = atomic_load_explicit(&self->peak_offset, memory_order_relaxed);
                    while (new_pos > current_peak) {
                        if (atomic_compare_exchange_weak_explicit(&self->peak_offset, &current_peak, new_pos,
                                                                  memory_order_relaxed, memory_order_relaxed)) {
                            break;
                        }
                    }

                    // Might have been poisoned by fl_arena_rewind
                    asan_unpoison_memory_region(return_ptr, size);

                    return return_ptr;
                }

                u64 required_committed = align_pow2(new_pos, self->page_size);
                if (FL_UNLIKELY(required_committed > self->reserved_size)) {
                    log_fatal("Out of reserved memory in arena: %s:%d", self->allocation_site_file,
                              self->allocation_site_line);
                    fl_log_flush();
                    exit(1);
                }

                u64 commit_size = required_committed - current_committed;
                if (commit_size == 0) {
                    break; // No need to commit, recheck current_committed
                }

                ArenaError result = commit_memory_os((char*)self->ptr + current_committed, commit_size);
                if (FL_UNLIKELY(result != ARENA_SUCCESS)) {
                    log_fatal("Unable to commit memory arena: %s:%d", self->allocation_site_file,
                              self->allocation_site_line);
                    fl_log_flush();
                    exit(1);
                }

                atomic_compare_exchange_strong(&self->committed_size, &current_committed, required_committed);
                // If CAS fails, another thread may have committed more, so loop again
            }

            void* return_ptr = (char*)self->ptr + aligned_pos;

            u64 current_peak = atomic_load_explicit(&self->peak_offset, memory_order_relaxed);
            while (new_pos > current_peak) {
                if (atomic_compare_exchange_weak_explicit(&self->peak_offset, &current_peak, new_pos,
                                                          memory_order_relaxed, memory_order_relaxed)) {
                    break;
                }
            }

            // Might have been poisoned by fl_arena_rewind
            asan_unpoison_memory_region(return_ptr, size);

            return return_ptr;
        }
        // CAS failed, another thread updated position - retry
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* fl_arena_alloc_raw_zero(FlArena* self, u64 size, u64 alignment) {
    void* ptr = fl_arena_alloc_raw(self, size, alignment);
    memory_zero(ptr, size);
    return ptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Single-threaded allocation - use only when arena is not shared between threads

void* fl_arena_alloc_raw_st(FlArena* self, u64 size, u64 alignment) {
    u64 current_pos = atomic_load_explicit(&self->pos, memory_order_relaxed);
    const u64 aligned_pos = align_pow2(current_pos, alignment);

    // Same overflow guard as the CAS path.
    if (FL_UNLIKELY(size > UINT64_MAX - aligned_pos)) {
        log_fatal("Out of reserved memory in arena (allocation size overflow): %s:%d", self->allocation_site_file,
                  self->allocation_site_line);
        fl_log_flush();
        exit(1);
    }

    const u64 new_pos = aligned_pos + size;

    u64 current_committed = atomic_load_explicit(&self->committed_size, memory_order_relaxed);
    if (FL_UNLIKELY(new_pos > current_committed)) {
        const u64 required_commit = new_pos - current_committed;
        const u64 commit_size = align_pow2(required_commit, self->page_size);

        if (FL_UNLIKELY(current_committed + commit_size > self->reserved_size)) {
            log_fatal(
                "Out of reserved memory in arena: %s:%d (requested: %.2f MB, committed: %.2f MB, reserved: %.2f MB)",
                self->allocation_site_file, self->allocation_site_line, size / (1024.0 * 1024.0),
                current_committed / (1024.0 * 1024.0), self->reserved_size / (1024.0 * 1024.0));
            fl_log_flush();
            exit(1);
        }

        ArenaError result = commit_memory_os((char*)self->ptr + current_committed, commit_size);
        if (FL_UNLIKELY(result != ARENA_SUCCESS)) {
            log_fatal("Unable to commit memory arena: %s:%d", self->allocation_site_file, self->allocation_site_line);
            fl_log_flush();
            exit(1);
        }

        atomic_store_explicit(&self->committed_size, current_committed + commit_size, memory_order_relaxed);
    }

    void* return_ptr = (char*)self->ptr + aligned_pos;
    atomic_store_explicit(&self->pos, new_pos, memory_order_relaxed);

    u64 current_peak = atomic_load_explicit(&self->peak_offset, memory_order_relaxed);
    if (new_pos > current_peak) {
        atomic_store_explicit(&self->peak_offset, new_pos, memory_order_relaxed);
    }

    // Might have been poisoned by fl_arena_rewind
    asan_unpoison_memory_region(return_ptr, size);

    return return_ptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* fl_arena_alloc_raw_zero_st(FlArena* self, u64 size, u64 alignment) {
    void* ptr = fl_arena_alloc_raw_st(self, size, alignment);
    memory_zero(ptr, size);
    return ptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_arena_rewind(FlArena* self) {
    // Everything in [rewind_pos, pos) becomes logically freed; poison it to detect use-after-rewind.
    u64 current_pos = atomic_load_explicit(&self->pos, memory_order_relaxed);

    if (current_pos > self->rewind_pos) {
        void* poison_start = (char*)self->ptr + self->rewind_pos;
        u64 poison_size = current_pos - self->rewind_pos;
        asan_poison_memory_region(poison_start, poison_size);
    }

    vm_range_rewind(self);

    // The header area below the rewind floor stays in use
    asan_unpoison_memory_region(self->ptr, self->rewind_pos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_rewind_to(FlArena* self, const void* pos) {
    FL_ASSERT(self != nullptr);

    u64 target = (u64)((const u8*)pos - (const u8*)self->ptr);
    if (target < self->rewind_pos) {
        target = self->rewind_pos;
    }

    u64 current_pos = atomic_load_explicit(&self->pos, memory_order_relaxed);
    if (current_pos > target) {
        asan_poison_memory_region((char*)self->ptr + target, current_pos - target);
        atomic_store_explicit(&self->pos, target, memory_order_relaxed);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_arena_destroy(FlArena* self) {
    FL_VALIDATE(self != nullptr);

    arena_tracker_unregister(self);

    // release_range_os frees the committed pages too, so no separate decommit is needed. The arena
    // header lives inside the released range, so read the fields into locals first.
    void* ptr = self->ptr;
    u64 reserved_size = self->reserved_size;
    release_range_os(ptr, reserved_size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlArenaMt* arena_mt_new_(const ArenaSettings* settings) {
    FlArena* arena = arena_new_(settings);

    FlArenaMt* arena_mt = arena_alloc(arena, FlArenaMt);
    arena_mt->arena = arena;

    // The wrapper lives at the arena base; raise the rewind floor past it so a full
    // fl_arena_rewind of the underlying arena can never clobber the wrapper itself.
    u64 current_pos = atomic_load_explicit(&arena->pos, memory_order_relaxed);
    arena->rewind_pos = current_pos;

    u64 current_committed = atomic_load_explicit(&arena->committed_size, memory_order_relaxed);
    atomic_store(&arena_mt->atomic_pos, current_pos);
    atomic_store(&arena_mt->atomic_committed_size, current_committed);

    return arena_mt;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* arena_mt_alloc_raw(FlArenaMt* self, u64 size, u64 alignment) {
    return fl_arena_alloc_raw(self->arena, size, alignment);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_mt_destroy(FlArenaMt* self) {
    fl_arena_destroy(self->arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_scratch_init(void) {
    if (s_arena_scratch != nullptr) {
        return;
    }
    s_arena_scratch = arena_new();
    s_arena_scratch->next = arena_new();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_scratch_destroy(void) {
    if (s_arena_scratch) {
        if (s_arena_scratch->next) {
            fl_arena_destroy(s_arena_scratch->next);
            s_arena_scratch->next = nullptr;
        }
        fl_arena_destroy(s_arena_scratch);
        s_arena_scratch = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlTempArena fl_arena_scratch_begin(void) {
    if (s_arena_scratch == nullptr) {
        arena_scratch_init();
    }

    FlTempArena temp_arena;
    temp_arena.arena = s_arena_scratch;
    temp_arena.pos = atomic_load_explicit(&temp_arena.arena->pos, memory_order_relaxed);
    return temp_arena;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlTempArena fl_arena_scratch_begin_conflict(const FlArena* self) {
    if (s_arena_scratch == nullptr) {
        arena_scratch_init();
    }

    FlTempArena temp_arena;
    if (s_arena_scratch == self) {
        temp_arena.arena = s_arena_scratch->next;
    } else {
        temp_arena.arena = s_arena_scratch;
    }

    temp_arena.pos = atomic_load_explicit(&temp_arena.arena->pos, memory_order_relaxed);
    return temp_arena;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_arena_align(FlArena* self, u64 alignment) {
    u64 current_pos = atomic_load_explicit(&self->pos, memory_order_relaxed);
    const u64 aligned_pos = align_pow2(current_pos, alignment);

    // Rounding up can wrap for a pathological alignment; a wrapped aligned_pos would slip
    // past the reserved-size check below. Reject it like an out-of-reservation request.
    if (FL_UNLIKELY(aligned_pos < current_pos)) {
        log_fatal("Out of reserved memory in arena (alignment overflow): %s:%d", self->allocation_site_file,
                  self->allocation_site_line);
        fl_log_flush();
        exit(1);
    }

    u64 current_committed = atomic_load_explicit(&self->committed_size, memory_order_relaxed);
    if (FL_UNLIKELY(aligned_pos > current_committed)) {
        // Crossing into not-yet-committed pages is normal, not an OOM: commit on demand and
        // only treat running past the reservation itself as fatal.
        const u64 required_committed = align_pow2(aligned_pos, self->page_size);
        if (FL_UNLIKELY(required_committed > self->reserved_size)) {
            log_fatal("Out of reserved memory in arena: %s:%d", self->allocation_site_file, self->allocation_site_line);
            fl_log_flush();
            exit(1);
        }

        ArenaError result
            = commit_memory_os((char*)self->ptr + current_committed, required_committed - current_committed);
        if (FL_UNLIKELY(result != ARENA_SUCCESS)) {
            log_fatal("Unable to commit memory arena: %s:%d", self->allocation_site_file, self->allocation_site_line);
            fl_log_flush();
            exit(1);
        }

        // If the CAS fails another thread committed concurrently; the pages we need are
        // committed either way, so no retry loop is required here.
        atomic_compare_exchange_strong(&self->committed_size, &current_committed, required_committed);
    }

    atomic_store_explicit(&self->pos, aligned_pos, memory_order_relaxed);
}
