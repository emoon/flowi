#include "linear_allocator.h"
#include "internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LinearAllocator_init(LinearAllocator* alloc, const char* id, u8* data, int len) {
	alloc->id = id;
	alloc->start_data = data;
	alloc->end_data = data + len;
	alloc->current_data = data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LinearAllocator_rewind(LinearAllocator* alloc) {
	alloc->current_data = alloc->start_data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u8* LinearAllocator_internal_alloc(LinearAllocator* s, int size, int alignment) {
	u8* start = s->current_data;

	// Align the pointer up to correct alignment
	start = (u8*)(((uintptr_t)start + (alignment - 1)) & -alignment);

	u8* end = start + size;

	if (end > s->end_data) {
		ERROR_ADD(FlError_Memory, "Ran out of memory in linear allocator %s", s->id);
		return NULL;
	}

	s->current_data = end;
	return start;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u8* LinearAllocator_internal_alloc_zero(LinearAllocator* s, int size, int alignment) {
	u8* data = LinearAllocator_internal_alloc(s, size, alignment);

	if (data) {
		memset(data, 0, size);
	}

	return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// If we ran out of memory it's possible to set new pointers and continue on from where we were

void LinearAllocator_update_resize(LinearAllocator* alloc, u8* data, int new_len) {
	int current_location = LinearAllocator_current_position(alloc);
	alloc->start_data = data;
	alloc->end_data = data + new_len;
	alloc->current_data = data + current_location;
}


