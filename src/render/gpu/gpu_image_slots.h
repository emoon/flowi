#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: sampler-slot bookkeeping for the non-atlas image paths - large static images (past
// GPU_IMAGE_ATLAS_MAX_DIM, each on its own texture) and dynamic images (IR source kind 1: a live neutral
// RGBA/alpha buffer streamed to a dedicated texture on frames a command references it). Texture creation, uploads,
// and unit binding are the backend's; tex is an opaque name here.
//
// Slot assignment is per-frame, first reference wins a slot, repeat references dedup to it. When every slot is
// taken, further slot-needing commands drop in stream order and overflow counts them. The raster pass fetches slot
// indices workgroup-uniformly, so indexing the shader's sampler array with them is legal.
//
// Dynamic entries are keyed by the user's buffer pointer. A referencing frame marks the entry dirty for re-upload;
// unreferenced frames upload nothing. Entries unreferenced for GPU_IMAGE_DYNAMIC_EVICT_FRAMES are evicted at frame
// start. Textures freed off the GL path queue on a dead list the renderer drains with the context current.

#include <core/types.h>
#include <ui/ui_types.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Number of large/dynamic image sampler slots.

#define GPU_IMAGE_SLOT_COUNT 11
#define GPU_IMAGE_SLOT_TABLE_CAP 64        // live large/dynamic textures per backend
#define GPU_IMAGE_DYNAMIC_EVICT_FRAMES 120 // unreferenced dynamic entries older than this evict at frame start

typedef struct GpuSlotImage {
    bool is_dynamic;
    FlImageHandle handle; // static identity (is_dynamic == false)
    const void* pixels;   // dynamic identity AND the referencing frame's upload source (zero-copy)
    u32 w;
    u32 h;
    u32 stride;      // dynamic: bytes per row of pixels (0 = tightly packed)
    bool alpha_only; // dynamic: single-channel coverage buffer (FlCmdImage mode 1)
    u32 tex;         // opaque backend texture name; 0 = not realized yet
    bool dirty;      // dynamic: a command referenced it this frame - content must upload before the dispatch
    u64 last_ref;    // dynamic: frame of the latest reference (eviction bookkeeping)
} GpuSlotImage;

typedef struct GpuImageSlots {
    GpuSlotImage* images; // live entries, dense (release/evict swap-removes)
    u32 image_count;
    u64 frame; // bumped once per rendered frame by gpu_image_slots_begin_frame

    // Per-frame slot table, rebuilt by the translator (reset before its single pass).
    u32 slot_image[GPU_IMAGE_SLOT_COUNT]; // index into images per assigned slot
    u8 slot_filter[GPU_IMAGE_SLOT_COUNT]; // 0 = linear, 1 = nearest - the slot's first reference decides
    u32 slot_count;
    u32 overflow; // commands dropped this frame because every slot was taken

    // Textures freed off the GL path; the renderer drains and deletes them with the context current.
    u32* dead_tex;
    u32 dead_count;
    bool table_full_diagnosed; // one table-full diagnostic per frame, not one per record
} GpuImageSlots;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_image_slots_init(GpuImageSlots* slots, struct FlArena* arena);

// Per-rendered-frame bookkeeping, called before translation: advance the frame counter and evict dynamic
// entries unreferenced for GPU_IMAGE_DYNAMIC_EVICT_FRAMES (their textures join the dead list). Eviction
// happens only here - never mid-translation, where the slot table holds indices into images.
void gpu_image_slots_begin_frame(GpuImageSlots* slots);

// Reset the per-frame slot table and overflow counter. The translator calls this once before its pass.
void gpu_image_slots_frame_reset(GpuImageSlots* slots);

// Register a large static image the prepare seam realized into its own texture. Returns the entry index, or
// -1 when the table is full (logged; the handle simply never renders).
int gpu_image_slots_register_static(GpuImageSlots* slots, FlImageHandle handle, u32 w, u32 h, u32 tex);

// Resolve a static handle to its entry index, -1 when absent (never prepared, atlas-sized, or released).
int gpu_image_slots_find_static(const GpuImageSlots* slots, FlImageHandle handle);

// Drop a static entry: the handle stops resolving and its texture joins the dead list. A handle that was
// never registered is a no-op.
void gpu_image_slots_release_static(GpuImageSlots* slots, FlImageHandle handle);

// Find-or-create the dynamic entry for a buffer and mark it referenced this frame (dirty + last_ref). A
// storage-shape change (dims or mode) queues the old texture on the dead list and resets tex so the
// renderer recreates it. Returns the entry index, or -1 when the table is full (logged once per frame).
// Idempotent within a frame - repeat references to the same buffer touch identically.
int gpu_image_slots_touch_dynamic(GpuImageSlots* slots, const void* pixels, u32 w, u32 h, u32 stride, bool alpha_only);

// Claim this frame's slot for an entry (repeat references return the existing slot). Returns the slot
// index, or -1 when every slot is taken - the caller drops the command and overflow counts it.
int gpu_image_slots_assign(GpuImageSlots* slots, u32 image_index, u8 filter);

// Drain the dead-texture list: *out receives the names, the count returns, and the list empties. The
// renderer deletes them with the GL context current.
u32 gpu_image_slots_take_dead(GpuImageSlots* slots, const u32** out);
