#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core image readiness registry.
//
// A small, backend-agnostic map from image handle -> realized logical dimensions. It holds NO pixels - the
// decoded/realized pixels live in the active backend (the SW TLSF pool, the GPU atlas/slots). Presence in
// the registry IS readiness: an image is ready only once the active backend has acked it via
// image_registry_mark_realized. Pending / error are NOT tracked here - they stay in the load/decode path
// (image.c reads the VFS status for the not-yet-realized case).
//
// mark_realized runs on the job-worker thread (SW) or the render thread (GPU); the readiness query runs on the
// main thread. An atomic singleton makes the instance pointer race-free and a mutex serialises map access.

#include "image.h"
#include <stdbool.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ImageRegistry ImageRegistry;

/// Create the registry, allocated in the caller's arena. The first registry created becomes the process-wide
/// singleton the backend acks reach; one created while another is installed stays private and receives none.
ImageRegistry* image_registry_new(struct FlArena* arena);

/// Destroy the registry. Clears the singleton only if this registry is the installed one; the map lives in
/// the caller's arena.
void image_registry_destroy(ImageRegistry* self);

/// Backend-agnostic realization ack. Called at the END of each backend's prepare_image once the handle's
/// pixels are realized into the backend's representation (SW pool / GPU atlas-or-slot). Marks the handle
/// ready with its logical dimensions.
void image_registry_mark_realized(ImageHandle handle, int width, int height);

/// Drop a handle's realization. Idempotent.
void image_registry_remove(ImageHandle handle);

/// Returns true iff the active backend has realized the handle; fills logical dimensions (out_width /
/// out_height may be null). Safe to call on the main thread concurrently with backend acks.
bool image_registry_is_ready(ImageHandle handle, int* out_width, int* out_height);
