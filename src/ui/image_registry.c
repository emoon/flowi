#include "image_registry.h"
#include <core/arena.h>
#include <core/assert.h>
#include <core/hashmap.h>
#include <core/os/os.h>
#include <stdatomic.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A handle's realized form as the active backend reported it: logical dimensions only, no pixels. Presence in
// the map is readiness - the entry exists only after a backend acked realization.

typedef struct ImageRealization {
    int width;
    int height;
} ImageRealization;

struct ImageRegistry {
    FlArena* arena;
    Mutex mutex;
    hashmap(ImageHandle, ImageRealization) realized;
};

// Written by the main thread (new/destroy), read by the backend ack on the job-worker thread (SW) / render
// thread (GPU). The mutex guards the map itself.
static _Atomic(struct ImageRegistry*) s_image_registry;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImageRegistry* image_registry_new(FlArena* arena) {
    ImageRegistry* self = arena_alloc_zero(arena, ImageRegistry);
    self->arena = arena;
    mutex_init(&self->mutex);
    hashmap_new(&self->realized, arena, 64);
    struct ImageRegistry* installed = nullptr;
    atomic_compare_exchange_strong(&s_image_registry, &installed, self);
    return self;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void image_registry_destroy(ImageRegistry* self) {
    FL_VALIDATE(self != nullptr);

    if (atomic_load(&s_image_registry) == self) {
        atomic_store(&s_image_registry, nullptr);
    }

    mutex_destroy(&self->mutex);
    // The map lives in the caller-owned arena; nothing else to free here.
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void image_registry_mark_realized(ImageHandle handle, int width, int height) {
    ImageRegistry* self = atomic_load(&s_image_registry);
    if (!self || handle == IMAGE_HANDLE_INVALID) {
        return;
    }

    mutex_lock_auto(&self->mutex);
    ImageRealization entry = { .width = width, .height = height };
    hashmap_insert(&self->realized, handle, entry);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void image_registry_remove(ImageHandle handle) {
    ImageRegistry* self = atomic_load(&s_image_registry);
    if (!self || handle == IMAGE_HANDLE_INVALID) {
        return;
    }

    mutex_lock_auto(&self->mutex);
    hashmap_remove(&self->realized, handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool image_registry_is_ready(ImageHandle handle, int* out_width, int* out_height) {
    ImageRegistry* self = atomic_load(&s_image_registry);
    if (!self || handle == IMAGE_HANDLE_INVALID) {
        return false;
    }

    mutex_lock_auto(&self->mutex);
    ImageRealization* entry = hashmap_get(&self->realized, handle);
    if (!entry) {
        return false;
    }

    if (out_width) {
        *out_width = entry->width;
    }
    if (out_height) {
        *out_height = entry->height;
    }
    return true;
}
