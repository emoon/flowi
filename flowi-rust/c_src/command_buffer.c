#include "command_buffer.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create a new command buffer to fill with commands

bool CommandBuffer_create(CommandBuffer* self, const char* id, struct FlAllocator* allocator, int len) {
    self->command_count = 0;
    self->read_ptr = 0;
    return LinearAllocator_create_with_allocator(&self->allocator, id, allocator, len, true);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destroy the command buffer

void CommandBuffer_destroy(CommandBuffer* self) {
    LinearAllocator_destroy(&self->allocator);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rewind to start populating/allocating from the buffer again

void CommandBuffer_rewind(CommandBuffer* self) {
    LinearAllocator_rewind(&self->allocator);
    self->command_count = 0;
    self->read_ptr = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Allocates a command in the command buffer for the user to fill out

u8* CommandBuffer_alloc_cmd(CommandBuffer* self, u16 cmd, u32 size) {
    u8* cmd_data = LinearAllocator_internal_alloc_unaligned(&self->allocator, size + 6);

    *((u16*)(cmd_data + 0)) = cmd;
    *((u32*)(cmd_data + 2)) = size;

    self->command_count++;

    return cmd_data + 6;
}
