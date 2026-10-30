#pragma once

// Support shared by the VFS test files (unit_tests_vfs*.c): in-memory zip and LHA writers for archives whose shape a
// case controls, the location of the checked-in fixtures, and checks that read, list or fail a path through a mount.

#include "unit_tests_common.h"

#include <flowi/vfs/drivers.h>
#include <vfs/drivers/archive_drivers.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Fixtures live in tests/fixtures/vfs. The directory comes from the build, so it does not depend on the working
// directory.
#define VFS_FIXTURE_DIR FLOWI_TEST_FIXTURE_DIR "/vfs"

static inline FlString vfs_fixture_path(FlArena* arena, FlString name) {
    return fl_sprintf_arena(arena, "%s/%S", VFS_FIXTURE_DIR, name);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The three loose text files in the fixture directory, and the first three entries of test.zip, which hold the same
// text.
#define FIXTURE_ASYNC_COUNT 3
static const char* const k_fixture_async_names[FIXTURE_ASYNC_COUNT]
    = { "async_test1.txt", "async_test2.txt", "async_test3.txt" };
static const char* const k_fixture_async_contents[FIXTURE_ASYNC_COUNT]
    = { "Content 1", "Content 2 is longer", "Content 3" };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define BYTE_WRITER_MIN_CAPACITY 4096

// A growing byte buffer that lives in an arena, so a test frees all of its archives by destroying the arena.
typedef struct ByteWriter {
    FlArena* arena;
    u8* bytes;
    size_t size;
    size_t capacity;
} ByteWriter;

typedef struct ZipEntry {
    const char* name;
    const u8* data;
    size_t size;
} ZipEntry;

static inline ByteWriter* byte_writer_create(FlArena* arena) {
    ByteWriter* writer = arena_alloc_zero(arena, ByteWriter);
    writer->arena = arena;
    return writer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void byte_writer_reserve(ByteWriter* writer, size_t extra) {
    if (writer->size + extra <= writer->capacity) {
        return;
    }
    size_t capacity = max(writer->capacity * 2, max(writer->size + extra, (size_t)BYTE_WRITER_MIN_CAPACITY));
    u8* bytes = arena_alloc_array(writer->arena, u8, capacity);
    if (writer->size > 0) {
        memcpy(bytes, writer->bytes, writer->size);
    }
    writer->bytes = bytes;
    writer->capacity = capacity;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void byte_put(ByteWriter* writer, u64 value, int byte_count) {
    byte_writer_reserve(writer, (size_t)byte_count);
    for (int i = 0; i < byte_count; i++) {
        writer->bytes[writer->size++] = (u8)(value >> (8 * i));
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void byte_put_bytes(ByteWriter* writer, const void* data, size_t size) {
    byte_writer_reserve(writer, size);
    if (size > 0) {
        memcpy(writer->bytes + writer->size, data, size);
    }
    writer->size += size;
}

#define ZIP_LOCAL_SIGNATURE 0x04034b50u
#define ZIP_CENTRAL_SIGNATURE 0x02014b50u
#define ZIP_END_SIGNATURE 0x06054b50u
#define ZIP_VERSION 20
#define ZIP_DOS_DATE_1980 0x21

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Writes a zip of stored entries. A name ending in '/' is a directory entry and takes no data.
static inline void zip_build(ByteWriter* zip, const ZipEntry* entries, int entry_count) {
    size_t* local_offsets = arena_alloc_array(zip->arena, size_t, entry_count);

    for (int i = 0; i < entry_count; i++) {
        size_t name_size = strlen(entries[i].name);
        size_t data_size = entries[i].size;
        local_offsets[i] = zip->size;
        byte_put(zip, ZIP_LOCAL_SIGNATURE, 4);
        byte_put(zip, ZIP_VERSION, 2);
        byte_put(zip, 0, 2); // flags
        byte_put(zip, 0, 2); // stored
        byte_put(zip, 0, 2); // time
        byte_put(zip, ZIP_DOS_DATE_1980, 2);
        byte_put(zip, fl_hash_crc32(entries[i].data, data_size), 4);
        byte_put(zip, data_size, 4);
        byte_put(zip, data_size, 4);
        byte_put(zip, name_size, 2);
        byte_put(zip, 0, 2); // extra
        byte_put_bytes(zip, entries[i].name, name_size);
        byte_put_bytes(zip, entries[i].data, data_size);
    }

    size_t central_offset = zip->size;
    for (int i = 0; i < entry_count; i++) {
        size_t name_size = strlen(entries[i].name);
        size_t data_size = entries[i].size;
        byte_put(zip, ZIP_CENTRAL_SIGNATURE, 4);
        byte_put(zip, ZIP_VERSION, 2); // made by
        byte_put(zip, ZIP_VERSION, 2); // needed
        byte_put(zip, 0, 2);           // flags
        byte_put(zip, 0, 2);           // stored
        byte_put(zip, 0, 2);           // time
        byte_put(zip, ZIP_DOS_DATE_1980, 2);
        byte_put(zip, fl_hash_crc32(entries[i].data, data_size), 4);
        byte_put(zip, data_size, 4);
        byte_put(zip, data_size, 4);
        byte_put(zip, name_size, 2);
        byte_put(zip, 0, 2); // extra
        byte_put(zip, 0, 2); // comment
        byte_put(zip, 0, 2); // disk
        byte_put(zip, 0, 2); // internal attributes
        byte_put(zip, 0, 4); // external attributes
        byte_put(zip, local_offsets[i], 4);
        byte_put_bytes(zip, entries[i].name, name_size);
    }

    size_t central_size = zip->size - central_offset;
    byte_put(zip, ZIP_END_SIGNATURE, 4);
    byte_put(zip, 0, 2);
    byte_put(zip, 0, 2);
    byte_put(zip, entry_count, 2);
    byte_put(zip, entry_count, 2);
    byte_put(zip, central_size, 4);
    byte_put(zip, central_offset, 4);
    byte_put(zip, 0, 2); // comment
}

#define ZIP_TEXT(name, text) ((ZipEntry) { name, (const u8*)(text), strlen(text) })
#define ZIP_FILE(name, zip_writer) ((ZipEntry) { name, (zip_writer)->bytes, (zip_writer)->size })
#define ZIP_DIR(name) ((ZipEntry) { name, (const u8*)"", 0 })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// LHA archives of stored entries, for the Lhasa driver.
// Bytes of a level 0 header after its first two (size, checksum) that do not depend on the name.
#define LHA_HEADER_FIXED 22
#define LHA_METHOD_STORED "-lh0-"
#define LHA_METHOD_DIRECTORY "-lhd-"
#define LHA_ATTRIBUTE_FILE 0x20
#define LHA_ATTRIBUTE_DIRECTORY 0x10
#define LHA_CRC_POLYNOMIAL 0xA001u

static inline u16 lha_crc16(const u8* data, size_t size) {
    u16 crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (u16)((crc >> 1) ^ LHA_CRC_POLYNOMIAL) : (u16)(crc >> 1);
        }
    }
    return crc;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void lha_put_entry(ByteWriter* lha, const char* name, const char* text, const char* method,
                                 u8 attribute) {
    size_t name_size = strlen(name);
    size_t data_size = strlen(text);
    size_t header_start = lha->size;

    byte_put(lha, LHA_HEADER_FIXED + name_size, 1);
    byte_put(lha, 0, 1); // checksum, patched below
    byte_put_bytes(lha, method, strlen(method));
    byte_put(lha, data_size, 4); // packed
    byte_put(lha, data_size, 4); // original
    byte_put(lha, 0, 4);         // time
    byte_put(lha, attribute, 1);
    byte_put(lha, 0, 1); // header level 0
    byte_put(lha, name_size, 1);
    byte_put_bytes(lha, name, name_size);
    byte_put(lha, lha_crc16((const u8*)text, data_size), 2);

    u8 sum = 0;
    for (size_t i = header_start + 2; i < lha->size; i++) {
        sum = (u8)(sum + lha->bytes[i]);
    }
    lha->bytes[header_start + 1] = sum;

    byte_put_bytes(lha, text, data_size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void lha_put_end(ByteWriter* lha) {
    byte_put(lha, 0, 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// An archive holding one stored file.
static inline void lha_build(ByteWriter* lha, const char* name, const char* text) {
    lha_put_entry(lha, name, text, LHA_METHOD_STORED, LHA_ATTRIBUTE_FILE);
    lha_put_end(lha);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Starts from a pristine VFS with every archive driver this build contains registered.
static inline bool vfs_test_begin(void) {
    process_singletons_reset();
    if (!fl_vfs_init(s_arena)) {
        return false;
    }
    fl_vfs_register_builtin_drivers();
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Mounts path and waits for the mount job. nullptr when the mount did not become ready.
static inline FlVfsMount* vfs_test_mount(FlString path) {
    FlVfsMount* mount = vfs_mount(path).mount;
    vfs_wait_all();
    return mount != nullptr && fl_vfs_mount_is_ready(mount) ? mount : nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A fresh, empty directory under the working directory for a case to put files in. Empty when it cannot be made.
static inline FlString vfs_test_temp_dir(FlArena* arena, FlString name) {
    FlString dir = file_build_path_os(arena, get_current_directory_os(arena), name);
    file_remove_directory_recursive_os(dir);
    return file_create_directory_os(dir) ? dir : FL_S("");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline bool vfs_test_write_file(FlArena* arena, FlString dir, FlString name, const ByteWriter* bytes) {
    FlString path = file_build_path_os(arena, dir, name);
    return file_write_os(path, bytes->bytes, (i64)bytes->size) == (i64)bytes->size;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline bool vfs_expect_read(FlVfsMount* mount, FlString path, FlString expected) {
    FlVfsHandle handle = vfs_mount_read_all(mount, path);
    fl_vfs_wait(handle);
    FlVfsData data = fl_vfs_get_data(handle);
    bool ok = data.success && (size_t)data.size == expected.length
              && memcmp(data.data, expected.data, expected.length) == 0;
    if (!ok) {
        printf("read of '%.*s' failed (success=%d size=%lld)\n", (int)path.length, path.data, data.success,
               (long long)data.size);
    }
    fl_vfs_close(handle);
    return ok;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline bool vfs_expect_read_fails(FlVfsMount* mount, FlString path) {
    FlVfsHandle handle = vfs_mount_read_all(mount, path);
    fl_vfs_wait(handle);
    FlVfsData data = fl_vfs_get_data(handle);
    bool failed = !data.success && data.data == nullptr && data.size == 0;
    if (!failed) {
        printf("read of '%.*s' unexpectedly succeeded\n", (int)path.length, path.data);
    }
    fl_vfs_close(handle);
    return failed;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The entries of a listing as "name" for files, "name/" for directories and "name*" for files flagged as archives, in
// listing order, space separated.
static inline bool vfs_expect_list(FlVfsMount* mount, FlString path, const char* expected) {
    arena_scratch_auto(temp);
    FlVfsDirHandle handle = fl_vfs_get_listing(mount, path, 0);
    fl_vfs_wait(handle);
    FlVfsFileList files = fl_vfs_get_file_list(handle);

    FlString summary = FL_S("");
    if (files.success) {
        for_count(i, files.count) {
            const FlVfsEntry* entry = &files.entries[i];
            const char* mark = entry->is_directory ? "/" : (entry->is_archive ? "*" : "");
            summary = fl_sprintf_arena(temp.arena, "%S%s%S%s", summary, i > 0 ? " " : "", entry->name, mark);
        }
    }

    bool ok = files.success && fl_string_equals(summary, string_from_cstr(expected));
    if (!ok) {
        printf("list of '%.*s' gave success=%d '%.*s', expected '%s'\n", (int)path.length, path.data, files.success,
               (int)summary.length, summary.data, expected);
    }
    fl_vfs_close(handle);
    return ok;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Reads path through a file handle chunk bytes at a time. Every read but the last must fill the buffer, the last must
// bring the file to exactly expected, and the read after it must report the end with zero bytes.
static inline bool vfs_expect_chunked_read(FlVfsMount* mount, FlString path, FlString expected, size_t chunk) {
    arena_scratch_auto(temp);
    if (chunk == 0) {
        return false;
    }
    FlVfsHandle file = fl_vfs_mount_open(mount, path, FlVfsOpenFlags_Read);
    if (file == FL_VFS_HANDLE_INVALID) {
        return false;
    }
    fl_vfs_wait(file);

    bool ok = fl_vfs_is_ready(file);
    u8* buffer = arena_alloc_array(temp.arena, u8, chunk);
    size_t total = 0;
    while (ok) {
        FlVfsHandle read = fl_vfs_read(file, buffer, (i64)chunk);
        fl_vfs_wait(read);
        FlVfsData data = fl_vfs_get_data(read);
        size_t want = min(chunk, expected.length - total);
        ok = data.success && (size_t)data.size == want && memcmp(buffer, expected.data + total, want) == 0;
        fl_vfs_close(read);
        total += want;
        if (want == 0) {
            break;
        }
    }
    fl_vfs_close(file);
    if (!ok) {
        printf("chunked read of '%.*s' in %zu byte pieces failed after %zu bytes\n", (int)path.length, path.data, chunk,
               total);
    }
    return ok;
}
