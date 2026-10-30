#include "gpu_image_slots.h"
#include <core/arena.h>
#include <core/log.h>

// Double the table bounds the churn between drains: every live texture released plus their replacements.
#define GPU_IMAGE_DEAD_CAP (GPU_IMAGE_SLOT_TABLE_CAP * 2)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_image_slots_init(GpuImageSlots* slots, FlArena* arena) {
    *slots = (GpuImageSlots) { 0 };
    slots->images = arena_alloc_array(arena, GpuSlotImage, GPU_IMAGE_SLOT_TABLE_CAP);
    slots->dead_tex = arena_alloc_array(arena, u32, GPU_IMAGE_DEAD_CAP);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void push_dead(GpuImageSlots* slots, u32 tex) {
    if (tex == 0) {
        return;
    }
    if (slots->dead_count >= GPU_IMAGE_DEAD_CAP) {
        log_warning("gpu backend: image-slot dead list full; texture %u leaks until destroy", (unsigned)tex);
        return;
    }
    slots->dead_tex[slots->dead_count++] = tex;
}

// Only legal between frames (begin_frame / release paths) - the per-frame slot table holds indices into
// images.
static void remove_entry(GpuImageSlots* slots, u32 index) {
    push_dead(slots, slots->images[index].tex);
    slots->images[index] = slots->images[--slots->image_count];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_image_slots_begin_frame(GpuImageSlots* slots) {
    slots->frame++;
    slots->table_full_diagnosed = false;
    for (u32 i = 0; i < slots->image_count;) {
        const GpuSlotImage* img = &slots->images[i];
        if (img->is_dynamic && slots->frame - img->last_ref > GPU_IMAGE_DYNAMIC_EVICT_FRAMES) {
            remove_entry(slots, i);
            continue; // the swapped-in entry lands at i - re-test it
        }
        i++;
    }
}

void gpu_image_slots_frame_reset(GpuImageSlots* slots) {
    slots->slot_count = 0;
    slots->overflow = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int gpu_image_slots_find_static(const GpuImageSlots* slots, FlImageHandle handle) {
    for (u32 i = 0; i < slots->image_count; i++) {
        if (!slots->images[i].is_dynamic && slots->images[i].handle == handle) {
            return (int)i;
        }
    }
    return -1;
}

int gpu_image_slots_register_static(GpuImageSlots* slots, FlImageHandle handle, u32 w, u32 h, u32 tex) {
    if (gpu_image_slots_find_static(slots, handle) >= 0) {
        log_error("gpu backend: image handle %u registered twice as a slot image", (unsigned)handle);
        return -1;
    }
    if (slots->image_count >= GPU_IMAGE_SLOT_TABLE_CAP) {
        log_error("gpu backend: image-slot table full (%d) - large image %u dropped", GPU_IMAGE_SLOT_TABLE_CAP,
                  (unsigned)handle);
        return -1;
    }
    u32 index = slots->image_count++;
    slots->images[index] = (GpuSlotImage) { .handle = handle, .w = w, .h = h, .tex = tex };
    return (int)index;
}

void gpu_image_slots_release_static(GpuImageSlots* slots, FlImageHandle handle) {
    int index = gpu_image_slots_find_static(slots, handle);
    if (index >= 0) {
        remove_entry(slots, (u32)index);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int gpu_image_slots_touch_dynamic(GpuImageSlots* slots, const void* pixels, u32 w, u32 h, u32 stride, bool alpha_only) {
    GpuSlotImage* img = NULL;
    u32 index = 0;
    for (u32 i = 0; i < slots->image_count; i++) {
        if (slots->images[i].is_dynamic && slots->images[i].pixels == pixels) {
            img = &slots->images[i];
            index = i;
            break;
        }
    }
    if (img == NULL) {
        if (slots->image_count >= GPU_IMAGE_SLOT_TABLE_CAP) {
            if (!slots->table_full_diagnosed) {
                log_error("gpu backend: image-slot table full (%d) - dynamic image dropped", GPU_IMAGE_SLOT_TABLE_CAP);
                slots->table_full_diagnosed = true;
            }
            return -1;
        }
        index = slots->image_count++;
        img = &slots->images[index];
        *img = (GpuSlotImage) { .is_dynamic = true, .pixels = pixels };
    } else if (img->w != w || img->h != h || img->alpha_only != alpha_only) {
        // Storage shape changed: the renderer must recreate the texture before the next upload.
        push_dead(slots, img->tex);
        img->tex = 0;
    }
    img->w = w;
    img->h = h;
    img->stride = stride;
    img->alpha_only = alpha_only;
    img->dirty = true;
    img->last_ref = slots->frame;
    return (int)index;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int gpu_image_slots_assign(GpuImageSlots* slots, u32 image_index, u8 filter) {
    for (u32 s = 0; s < slots->slot_count; s++) {
        if (slots->slot_image[s] == image_index) {
            return (int)s; // already slotted this frame; the first reference's filter stands
        }
    }
    if (slots->slot_count >= GPU_IMAGE_SLOT_COUNT) {
        slots->overflow++;
        return -1;
    }
    u32 s = slots->slot_count++;
    slots->slot_image[s] = image_index;
    slots->slot_filter[s] = filter;
    return (int)s;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 gpu_image_slots_take_dead(GpuImageSlots* slots, const u32** out) {
    *out = slots->dead_tex;
    u32 count = slots->dead_count;
    slots->dead_count = 0;
    return count;
}
