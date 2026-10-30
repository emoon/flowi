#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Uniform List - Internal Implementation Header
//
// The implementation seam behind the public fl_list_uniform_* API: the context struct and the receiver-taking
// free functions the public ctx-free bodies forward into, plus the scroll introspection/drive and per-frame
// update hooks that stay internal.

#include <core/string.h> // FlString - the generated header takes it from its includer
#include <core/types.h>
#include <flowi/ui/list_uniform.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlArena FlArena;
typedef struct FlListUniformContext FlListUniformContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// arena backs internal state allocations.
FlListUniformContext* list_uniform_create(FlArena* arena);

void list_uniform_destroy(FlListUniformContext* self);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Receiver-taking bodies behind the public ctx-free fl_list_uniform_* API

FlListUniformRange list_uniform_begin(FlListUniformContext* self, FlString id, u32 total_count, FlListUniformOpts opts);
void list_uniform_item_begin(FlListUniformContext* self, u32 index);
void list_uniform_item_end(FlListUniformContext* self);
void list_uniform_end(FlListUniformContext* self);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scroll introspection / drive. Keyed by the container's string id so a caller may read/drive a list outside its
// begin->end scope. Both no-op / return 0 for an unknown id.

// Current smoothed scroll offset (pixels from the top of the content) for list id.
f32 list_uniform_scroll_offset(FlListUniformContext* self, FlString id);

// Nudge list id's smooth-scroll target (and offset, so the move is immediate and sticky) by delta_px, clamped
// to [0, max] for a uniform list of total_count rows spaced item_stride px apart. Positive scrolls toward the
// end.
void list_uniform_nudge_scroll(FlListUniformContext* self, FlString id, f32 delta_px, f32 item_stride, u32 total_count);

void list_uniform_begin_update(FlListUniformContext* self);
