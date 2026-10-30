// The UnLZX driver: an Amiga LZX archive through the VFS, with a test-side encoder to build members far larger than
// the checked-in fixture.

#include "vfs_test_support.h"

#if FLOWI_VFS_DRIVER_UNLZX

#include <unlzx.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A test-side LZX encoder, so members far larger than the checked-in fixture can be built on the fly. The member is a
// run of segments, each a literal followed by a match at offset 1 repeating it, coded in verbatim blocks whose
// literals take 9-bit codes and matches 10-bit ones.

#define LZX_INFO_HEADER_SIZE 10
#define LZX_ENTRY_HEADER_SIZE 31
#define LZX_PACK_MODE_NORMAL 2
#define LZX_MACHINE_AMIGA 0x0a
#define LZX_BLOCK_VERBATIM 2
#define LZX_PRETREE_SYMBOLS 20
#define LZX_LITERALS 256
#define LZX_MATCH_SYMBOLS 512
#define LZX_LITERAL_CODE_BITS 9
#define LZX_MATCH_CODE_BITS 10
#define LZX_MATCH_OFFSET_ONE_SLOT 1
#define LZX_MIN_MATCH 3
#define LZX_LENGTH_SLOTS 16
#define LZX_SEGMENTS_PER_BLOCK 8192

// Pretree symbols that set a code length from 0 to 9 and to 10, and the one that keeps the previous length
#define LZX_PRETREE_ZERO_TO_NINE 8
#define LZX_PRETREE_ZERO_TO_TEN 7
#define LZX_PRETREE_KEEP 0

static const u8 s_lzx_length_base[LZX_LENGTH_SLOTS] = { 0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192 };
static const u8 s_lzx_length_extra[LZX_LENGTH_SLOTS] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6 };

typedef struct LzxWriter {
    u8* data;
    u64 size;
    u32 word;
    u32 bits;
} LzxWriter;

typedef struct LzxTestArchive {
    u8* data;
    u64 size;
    u32 unpack_size;
    u32 crc;
} LzxTestArchive;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Segment i: its literal, and how many times the match repeats it
static u8 lzx_segment_literal(u32 i) {
    return (u8)(i * 131 + 7);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 lzx_segment_run(u32 i) {
    return LZX_MIN_MATCH + (i % 256);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LZX reads 16-bit big-endian words, taking bits from the least significant end

static void lzx_put_bits(LzxWriter* w, u32 value, u32 count) {
    for_count(i, count) {
        w->word |= ((value >> i) & 1) << w->bits;
        if (++w->bits == 16) {
            w->data[w->size++] = (u8)(w->word >> 8);
            w->data[w->size++] = (u8)w->word;
            w->word = 0;
            w->bits = 0;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Huffman codes go out most significant bit first

static void lzx_put_code(LzxWriter* w, u32 code, u32 length) {
    for (u32 i = length; i-- > 0;) {
        lzx_put_bits(w, (code >> i) & 1, 1);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A pretree of two 1-bit codes, then that code for every symbol of one pass over the literal lengths

static void lzx_put_length_pass(LzxWriter* w, u32 first, u32 second, u32 sent, u32 symbol_count) {
    for_count(i, LZX_PRETREE_SYMBOLS) {
        lzx_put_bits(w, (i == first || i == second) ? 1 : 0, 4);
    }
    for_count(i, symbol_count) {
        lzx_put_code(w, sent == first ? 0 : 1, 1);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void lzx_put_block_header(LzxWriter* w, u32 length, bool first_block) {
    lzx_put_bits(w, LZX_BLOCK_VERBATIM, 3);
    lzx_put_bits(w, (length >> 16) & 0xff, 8);
    lzx_put_bits(w, (length >> 8) & 0xff, 8);
    lzx_put_bits(w, length & 0xff, 8);

    if (first_block) {
        lzx_put_length_pass(w, LZX_PRETREE_ZERO_TO_TEN, LZX_PRETREE_ZERO_TO_NINE, LZX_PRETREE_ZERO_TO_NINE,
                            LZX_LITERALS);
        lzx_put_length_pass(w, LZX_PRETREE_ZERO_TO_TEN, LZX_PRETREE_ZERO_TO_NINE, LZX_PRETREE_ZERO_TO_TEN,
                            LZX_MATCH_SYMBOLS);
    } else {
        lzx_put_length_pass(w, LZX_PRETREE_KEEP, LZX_PRETREE_KEEP + 1, LZX_PRETREE_KEEP, LZX_LITERALS);
        lzx_put_length_pass(w, LZX_PRETREE_KEEP, LZX_PRETREE_KEEP + 1, LZX_PRETREE_KEEP, LZX_MATCH_SYMBOLS);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void lzx_put_segment(LzxWriter* w, u32 i) {
    lzx_put_code(w, lzx_segment_literal(i), LZX_LITERAL_CODE_BITS);

    u32 extra = lzx_segment_run(i) - LZX_MIN_MATCH;
    u32 slot = LZX_LENGTH_SLOTS - 1;
    while (s_lzx_length_base[slot] > extra) {
        slot--;
    }
    // Canonical codes: the 256 literals take 0..255 in 9 bits, so match symbol n takes 512 + n in 10 bits
    u32 match_symbol = (slot << 5) | LZX_MATCH_OFFSET_ONE_SLOT;
    lzx_put_code(w, (1u << LZX_LITERAL_CODE_BITS) + match_symbol, LZX_MATCH_CODE_BITS);
    lzx_put_bits(w, extra - s_lzx_length_base[slot], s_lzx_length_extra[slot]);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void lzx_put_le32(u8* out, u32 value) {
    for_count(i, 4) {
        out[i] = (u8)(value >> (8 * i));
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Build an archive holding one compressed member, name, of at least min_size bytes. crc_flip is xored into the
// stored CRC, so a non-zero value makes the member's data fail its check.

static LzxTestArchive lzx_build_archive(FlString name, u32 min_size, u32 crc_flip) {
    u32 segment_count = 0;
    u32 unpack_size = 0;
    u32 crc = 0;
    while (unpack_size < min_size) {
        u8 run[1 + LZX_MIN_MATCH + 255];
        u32 length = 1 + lzx_segment_run(segment_count);
        memset(run, lzx_segment_literal(segment_count), length);
        crc = hash_crc32_update(crc, run, length);
        unpack_size += length;
        segment_count++;
    }

    u64 header_size = LZX_INFO_HEADER_SIZE + LZX_ENTRY_HEADER_SIZE + name.length;
    LzxWriter w = { 0 };
    // At most 25 bits a segment, under 1024 bits a block header, and the final padding
    u32 block_count = (segment_count + LZX_SEGMENTS_PER_BLOCK - 1) / LZX_SEGMENTS_PER_BLOCK;
    w.data = calloc(1, header_size + (u64)segment_count * 4 + (u64)block_count * 128 + 8);
    w.size = header_size;

    for (u32 first = 0; first < segment_count; first += LZX_SEGMENTS_PER_BLOCK) {
        u32 end = min(first + LZX_SEGMENTS_PER_BLOCK, segment_count);
        u32 block_length = 0;
        for (u32 i = first; i < end; i++) {
            block_length += 1 + lzx_segment_run(i);
        }
        lzx_put_block_header(&w, block_length, first == 0);
        for (u32 i = first; i < end; i++) {
            lzx_put_segment(&w, i);
        }
    }
    // Pad out the last word, plus one more the decoder may read ahead into
    lzx_put_bits(&w, 0, 32 - w.bits);

    u8* info = w.data;
    info[0] = 'L';
    info[1] = 'Z';
    info[2] = 'X';

    u8* entry = w.data + LZX_INFO_HEADER_SIZE;
    lzx_put_le32(entry + 2, unpack_size);
    lzx_put_le32(entry + 6, (u32)(w.size - header_size));
    entry[10] = LZX_MACHINE_AMIGA;
    entry[11] = LZX_PACK_MODE_NORMAL;
    lzx_put_le32(entry + 22, crc ^ crc_flip);
    entry[30] = (u8)name.length;
    memcpy(entry + LZX_ENTRY_HEADER_SIZE, name.data, name.length);
    u32 header_crc = hash_crc32_update(0, entry, LZX_ENTRY_HEADER_SIZE + name.length);
    lzx_put_le32(entry + 26, header_crc);

    return (LzxTestArchive) { .data = w.data, .size = w.size, .unpack_size = unpack_size, .crc = crc };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define LZX_FIXTURE "test_unlzx_valid.lzx"
#define LZX_FIXTURE_MEMBER "test.txt"
#define LZX_FIXTURE_CONTENT "Hello from LZX test archive!\n"

// Where a test writes the archive it builds, so a failed run leaves one directory to remove.
static FlString lzx_temp_dir(FlArena* arena) {
    return vfs_test_temp_dir(arena, FL_S("unit_test_vfs_unlzx_tmp"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString lzx_write_temp_archive(FlArena* arena, FlString dir, LzxTestArchive archive) {
    FlString path = file_build_path_os(arena, dir, FL_S("member.lzx"));
    return file_write_os(path, archive.data, (i64)archive.size) == (i64)archive.size ? path : FL_S("");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_unlzx, a_member_of_the_checked_in_archive_lists_and_reads) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_test_mount(vfs_fixture_path(s_arena, FL_S(LZX_FIXTURE)));
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_list(mount, FL_S(""), LZX_FIXTURE_MEMBER));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S(LZX_FIXTURE_MEMBER), FL_S(LZX_FIXTURE_CONTENT)));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_unlzx, a_member_that_is_not_in_the_archive_fails_to_read) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_test_mount(vfs_fixture_path(s_arena, FL_S(LZX_FIXTURE)));
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_read_fails(mount, FL_S("nonexistent.txt")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_unlzx, a_file_named_lzx_that_is_not_an_archive_ends_in_a_mount_error) {
    ASSERT_TRUE(vfs_test_begin());

    FlArena* arena = arena_new();
    FlString dir = lzx_temp_dir(arena);
    ASSERT_TRUE(dir.length > 0);
    FlString path = file_build_path_os(arena, dir, FL_S("not_lzx.lzx"));
    ASSERT_EQ(file_write_os(path, (const u8*)"This is not an LZX archive", 26), 26);

    FlVfsMount* mount = vfs_mount(path).mount;
    ASSERT_NE(mount, nullptr);
    vfs_wait_all();
    ASSERT_EQ((int)fl_vfs_mount_get_status(mount), (int)FlVfsMountStatus_Error);
    ASSERT_TRUE(vfs_expect_read_fails(mount, FL_S("anything.txt")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
    file_remove_directory_recursive_os(dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_unlzx, a_buffer_shorter_than_the_member_takes_what_fits_and_the_next_read_carries_on) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_test_mount(vfs_fixture_path(s_arena, FL_S(LZX_FIXTURE)));
    ASSERT_NE(mount, nullptr);

    FlString content = FL_S(LZX_FIXTURE_CONTENT);
    ASSERT_TRUE(vfs_expect_chunked_read(mount, FL_S(LZX_FIXTURE_MEMBER), content, content.length - 1));
    ASSERT_TRUE(vfs_expect_chunked_read(mount, FL_S(LZX_FIXTURE_MEMBER), content, 1));
    ASSERT_TRUE(vfs_expect_chunked_read(mount, FL_S(LZX_FIXTURE_MEMBER), content, content.length + 1));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define LZX_SMALL_MEMBER_SIZE (300 * 1024)
#define LZX_ODD_CHUNK_SIZE 4097

UTEST(vfs_unlzx, chunks_that_do_not_line_up_with_the_decoder_blocks_join_up_into_the_member) {
    ASSERT_TRUE(vfs_test_begin());

    FlArena* arena = arena_new();
    FlString dir = lzx_temp_dir(arena);
    ASSERT_TRUE(dir.length > 0);
    LzxTestArchive archive = lzx_build_archive(FL_S("chunks.bin"), LZX_SMALL_MEMBER_SIZE, 0);
    FlString path = lzx_write_temp_archive(arena, dir, archive);
    ASSERT_TRUE(path.length > 0);

    FlVfsMount* mount = vfs_test_mount(path);
    ASSERT_NE(mount, nullptr);

    FlVfsHandle all_handle = vfs_mount_read_all(mount, FL_S("chunks.bin"));
    fl_vfs_wait(all_handle);
    FlVfsData all = fl_vfs_get_data(all_handle);
    ASSERT_TRUE(all.success);
    ASSERT_EQ(all.size, (i64)archive.unpack_size);
    ASSERT_EQ(hash_crc32_update(0, all.data, (u64)all.size), archive.crc);

    FlString whole = { .data = (const char*)all.data, .length = (u64)all.size };
    ASSERT_TRUE(vfs_expect_chunked_read(mount, FL_S("chunks.bin"), whole, LZX_ODD_CHUNK_SIZE));

    fl_vfs_close(all_handle);
    fl_vfs_mount_close(mount);
    vfs_destroy();
    free(archive.data);
    file_remove_directory_recursive_os(dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define LZX_CORRUPT_MEMBER_SIZE (100 * 1024)

UTEST(vfs_unlzx, a_member_with_a_bad_crc_fails_the_read_that_ends_it) {
    // The CRC can only be checked once the last byte is out, so the read that produces it fails instead of the stream
    // ending cleanly.
    ASSERT_TRUE(vfs_test_begin());

    FlArena* arena = arena_new();
    FlString dir = lzx_temp_dir(arena);
    ASSERT_TRUE(dir.length > 0);
    LzxTestArchive archive = lzx_build_archive(FL_S("corrupt.bin"), LZX_CORRUPT_MEMBER_SIZE, 1);
    FlString path = lzx_write_temp_archive(arena, dir, archive);
    ASSERT_TRUE(path.length > 0);

    FlVfsMount* mount = vfs_test_mount(path);
    ASSERT_NE(mount, nullptr);

    FlVfsHandle file = fl_vfs_mount_open(mount, FL_S("corrupt.bin"), FlVfsOpenFlags_Read);
    fl_vfs_wait(file);
    ASSERT_TRUE(fl_vfs_is_ready(file));

    u8 chunk[LZX_ODD_CHUNK_SIZE];
    i64 total = 0;
    bool failed = false;
    while (!failed) {
        FlVfsHandle read_handle = fl_vfs_read(file, chunk, LZX_ODD_CHUNK_SIZE);
        fl_vfs_wait(read_handle);
        FlVfsData result = fl_vfs_get_data(read_handle);
        fl_vfs_close(read_handle);
        failed = !result.success;
        if (!failed) {
            ASSERT_EQ(result.size, (i64)LZX_ODD_CHUNK_SIZE);
            total += result.size;
        }
    }
    // Every read before the failing one was full, and the failing one is the one that reaches the member's end.
    ASSERT_LT(total, (i64)archive.unpack_size);
    ASSERT_GE(total + LZX_ODD_CHUNK_SIZE, (i64)archive.unpack_size);

    FlVfsHandle all_handle = vfs_mount_read_all(mount, FL_S("corrupt.bin"));
    fl_vfs_wait(all_handle);
    ASSERT_FALSE(fl_vfs_get_data(all_handle).success);
    fl_vfs_close(all_handle);

    fl_vfs_close(file);
    fl_vfs_mount_close(mount);
    vfs_destroy();
    free(archive.data);
    file_remove_directory_recursive_os(dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define LZX_LARGE_MEMBER_SIZE (32u * 1024 * 1024)
#define LZX_HASH_CHUNK_SIZE (64 * 1024)

UTEST(vfs_unlzx, a_large_member_hashes_through_fixed_size_reads) {
    // The archive is far smaller than the member it holds, and reading the member in fixed chunks gives the same bytes
    // the encoder put in.
    ASSERT_TRUE(vfs_test_begin());

    FlArena* arena = arena_new();
    FlString dir = lzx_temp_dir(arena);
    ASSERT_TRUE(dir.length > 0);
    LzxTestArchive archive = lzx_build_archive(FL_S("large.bin"), LZX_LARGE_MEMBER_SIZE, 0);
    FlString path = lzx_write_temp_archive(arena, dir, archive);
    ASSERT_TRUE(path.length > 0);
    ASSERT_LT(archive.size, (u64)archive.unpack_size / 16);

    FlVfsMount* mount = vfs_test_mount(path);
    ASSERT_NE(mount, nullptr);

    FlVfsHandle file = fl_vfs_mount_open(mount, FL_S("large.bin"), FlVfsOpenFlags_Read);
    fl_vfs_wait(file);
    ASSERT_TRUE(fl_vfs_is_ready(file));

    u8* chunk = arena_alloc_array(arena, u8, LZX_HASH_CHUNK_SIZE);
    u32 crc = 0;
    i64 total = 0;
    for (;;) {
        FlVfsHandle read_handle = fl_vfs_read(file, chunk, LZX_HASH_CHUNK_SIZE);
        fl_vfs_wait(read_handle);
        FlVfsData result = fl_vfs_get_data(read_handle);
        fl_vfs_close(read_handle);
        ASSERT_TRUE(result.success);
        if (result.size == 0) {
            break;
        }
        crc = hash_crc32_update(crc, chunk, (u64)result.size);
        total += result.size;
    }
    ASSERT_EQ(total, (i64)archive.unpack_size);
    ASSERT_EQ(crc, archive.crc);

    fl_vfs_close(file);
    fl_vfs_mount_close(mount);
    vfs_destroy();
    free(archive.data);
    file_remove_directory_recursive_os(dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// An unlzx allocator that keeps count, so a test can see every allocation the decoder makes on any platform

#define COUNTING_HEADER_SIZE 16

typedef struct CountingAllocator {
    u64 live;
    u64 peak;
    u64 largest;
} CountingAllocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* counting_alloc(void* user, size_t size) {
    CountingAllocator* counter = (CountingAllocator*)user;
    u8* block = malloc(COUNTING_HEADER_SIZE + size);
    if (!block) {
        return nullptr;
    }
    memcpy(block, &size, sizeof(size));
    counter->live += size;
    counter->peak = max(counter->peak, counter->live);
    counter->largest = max(counter->largest, (u64)size);
    return block + COUNTING_HEADER_SIZE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void counting_free(void* user, void* ptr) {
    CountingAllocator* counter = (CountingAllocator*)user;
    u8* block = (u8*)ptr - COUNTING_HEADER_SIZE;
    size_t size = 0;
    memcpy(&size, block, sizeof(size));
    counter->live -= size;
    free(block);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Far above the decoder's fixed state, far below the member
#define LZX_DECODER_MEMORY_BOUND (1024 * 1024)

UTEST(vfs_unlzx, a_large_member_streams_through_bounded_decoder_allocations) {
    // Every allocation the decoder makes while streaming a member goes through the counting allocator, so a
    // member-sized one is caught however briefly it lives and whatever pages it reuses.
    LzxTestArchive archive = lzx_build_archive(FL_S("large.bin"), LZX_LARGE_MEMBER_SIZE, 0);
    CountingAllocator counter = { 0 };
    UnlzxAllocator allocator = { .alloc = counting_alloc, .free = counting_free, .user = &counter };

    UnlzxArchive* lzx = unlzx_open_memory_with_allocator(archive.data, (size_t)archive.size, &allocator);
    ASSERT_NE(lzx, nullptr);
    const UnlzxEntry* entry = unlzx_find_entry(lzx, "large.bin");
    ASSERT_NE(entry, nullptr);

    u64 live_before = counter.live;
    counter.peak = live_before;
    counter.largest = 0;

    UnlzxStream* stream = nullptr;
    ASSERT_EQ(unlzx_stream_open(lzx, entry, &stream), UNLZX_OK);

    u8* chunk = malloc(LZX_HASH_CHUNK_SIZE);
    ASSERT_NE(chunk, nullptr);
    u32 crc = 0;
    u64 total = 0;
    for (;;) {
        size_t bytes_read = 0;
        ASSERT_EQ(unlzx_stream_read(stream, chunk, LZX_HASH_CHUNK_SIZE, &bytes_read), UNLZX_OK);
        if (bytes_read == 0) {
            break;
        }
        crc = hash_crc32_update(crc, chunk, bytes_read);
        total += bytes_read;
    }
    ASSERT_EQ(total, (u64)archive.unpack_size);
    ASSERT_EQ(crc, archive.crc);
    ASSERT_LT(counter.largest, (u64)LZX_DECODER_MEMORY_BOUND);
    ASSERT_LT(counter.peak - live_before, (u64)LZX_DECODER_MEMORY_BOUND);

    unlzx_stream_close(stream);
    unlzx_close(lzx);
    ASSERT_EQ(counter.live, 0u);

    free(chunk);
    free(archive.data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_unlzx, an_empty_member_lists_as_a_file) {
    ASSERT_TRUE(vfs_test_begin());

    FlArena* arena = arena_new();
    FlString dir = lzx_temp_dir(arena);
    ASSERT_TRUE(dir.length > 0);
    LzxTestArchive archive = lzx_build_archive(FL_S("empty.bin"), 0, 0);
    ASSERT_EQ(archive.unpack_size, 0u);
    FlString path = lzx_write_temp_archive(arena, dir, archive);
    ASSERT_TRUE(path.length > 0);

    FlVfsMount* mount = vfs_test_mount(path);
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_list(mount, FL_S(""), "empty.bin"));

    fl_vfs_mount_close(mount);
    vfs_destroy();
    file_remove_directory_recursive_os(dir);
    free(archive.data);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_unlzx, opening_a_member_that_is_not_in_the_archive_fails_at_open) {
    const FlVfsPlugin* plugin = vfs_driver_unlzx();
    void* instance = nullptr;

    LzxTestArchive archive = lzx_build_archive(FL_S("member.bin"), LZX_SMALL_MEMBER_SIZE, 0);
    FlVfsOpenResult root = plugin->open_memory(instance, archive.data, archive.size, FL_S("member.lzx"));
    ASSERT_NE(root.handle, nullptr);

    FlVfsOpenResult missing = plugin->open(instance, root.handle, FL_S("missing.bin"), 0, (FlVfsMountOptions) { 0 });
    ASSERT_EQ(missing.handle, nullptr);
    ASSERT_EQ(missing.error.vfs_status, FlVfsStatus_FileNotFound);

    FlVfsOpenResult present = plugin->open(instance, root.handle, FL_S("member.bin"), 0, (FlVfsMountOptions) { 0 });
    ASSERT_NE(present.handle, nullptr);

    plugin->close(instance, present.handle);
    plugin->close(instance, root.handle);
    free(archive.data);
}

#endif // FLOWI_VFS_DRIVER_UNLZX
