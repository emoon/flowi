#include "utest.h"
#include "../src/command_buffer.h"
#include "../src/internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern struct FlContext* g_ctx;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(CommandBuffer, create_destroy) {
	CommandBuffer buffer;
	CommandBuffer_create(&buffer, "buffer", g_ctx->global_state->global_allocator, 256);
	CommandBuffer_destroy(&buffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(CommandBuffer, alloc_single_command) {
	CommandBuffer buffer;

	CommandBuffer_create(&buffer, "buffer", g_ctx->global_state->global_allocator, 256);
	u8* data = CommandBuffer_alloc_cmd(&buffer, 1, 4);
	*data = 0;

	CommandBuffer_destroy(&buffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(CommandBuffer, alloc_single_command_read_data) {
	CommandBuffer buffer;

	CommandBuffer_create(&buffer, "buffer", g_ctx->global_state->global_allocator, 256);
	u8* data = CommandBuffer_alloc_cmd(&buffer, 1, 4);
	*data++ = 3;
	*data++ = 4;
	*data++ = 5;
	*data++ = 6;

	ASSERT_EQ(CommandBuffer_begin_read_commands(&buffer), 1);

	const u8* command_data = 0;

	u16 cmd = CommandBuffer_read_next_cmd(&buffer, &command_data);

	ASSERT_EQ(cmd, 1);
	ASSERT_EQ(command_data[0], 3);
	ASSERT_EQ(command_data[1], 4);
	ASSERT_EQ(command_data[2], 5);
	ASSERT_EQ(command_data[3], 6);

	CommandBuffer_destroy(&buffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(CommandBuffer, alloc_two_command_read_data) {
	CommandBuffer buffer;

	CommandBuffer_create(&buffer, "buffer", g_ctx->global_state->global_allocator, 256);
	u8* data = CommandBuffer_alloc_cmd(&buffer, 1, 6);
	*data++ = 3;
	*data++ = 4;
	*data++ = 5;
	*data++ = 6;

	data = CommandBuffer_alloc_cmd(&buffer, 2, 4);
	*data++ = 9;
	*data++ = 9;
	*data++ = 9;
	*data++ = 9;

	ASSERT_EQ(CommandBuffer_begin_read_commands(&buffer), 2);

	const u8* command_data = 0;
	u16 cmd = CommandBuffer_read_next_cmd(&buffer, &command_data);

	ASSERT_EQ(cmd, 1);
	ASSERT_EQ(command_data[0], 3);
	ASSERT_EQ(command_data[1], 4);
	ASSERT_EQ(command_data[2], 5);
	ASSERT_EQ(command_data[3], 6);

	cmd = CommandBuffer_read_next_cmd(&buffer, &command_data);

	ASSERT_EQ(cmd, 2);
	ASSERT_EQ(command_data[0], 9);
	ASSERT_EQ(command_data[1], 9);
	ASSERT_EQ(command_data[2], 9);
	ASSERT_EQ(command_data[3], 9);

	CommandBuffer_destroy(&buffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(CommandBuffer, alloc_commands_rewind) {
	CommandBuffer buffer;

	CommandBuffer_create(&buffer, "buffer", g_ctx->global_state->global_allocator, 256);
	u8* data = CommandBuffer_alloc_cmd(&buffer, 1, 6);
	*data++ = 3;
	*data++ = 4;
	*data++ = 5;
	*data++ = 6;

	data = CommandBuffer_alloc_cmd(&buffer, 2, 4);
	*data++ = 9;
	*data++ = 9;
	*data++ = 9;
	*data++ = 9;

	ASSERT_EQ(CommandBuffer_begin_read_commands(&buffer), 2);

	CommandBuffer_rewind(&buffer);

	ASSERT_EQ(CommandBuffer_begin_read_commands(&buffer), 0);
	CommandBuffer_destroy(&buffer);
}


