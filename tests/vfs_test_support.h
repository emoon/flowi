#pragma once

// Support shared by the VFS test files (tests/vfs/*.c): in-memory zip and LHA writers for archives whose shape a
// case controls, the location of the checked-in fixtures, and checks that read, list or fail a path through a mount.

#include "unit_tests_common.h"

#include <flowi/vfs/drivers.h>
#include <vfs/drivers/archive_drivers.h>

// Fixtures live in tests/fixtures/vfs. The directory comes from the build, so it does not depend on the working
// directory.
#define VFS_FIXTURE_DIR FLOWI_TEST_FIXTURE_DIR "/vfs"

static inline FlString vfs_fixture_path(FlArena* arena, FlString name) {
    return fl_sprintf_arena(arena, "%s/%S", VFS_FIXTURE_DIR, name);
}

// The three loose text files in the fixture directory, and the first three entries of test.zip, which hold the same
// text.
#define FIXTURE_ASYNC_COUNT 3
static const char* const k_fixture_async_names[FIXTURE_ASYNC_COUNT]
    = { "async_test1.txt", "async_test2.txt", "async_test3.txt" };
static const char* const k_fixture_async_contents[FIXTURE_ASYNC_COUNT]
    = { "Content 1", "Content 2 is longer", "Content 3" };

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

static inline void byte_put(ByteWriter* writer, u64 value, int byte_count) {
    byte_writer_reserve(writer, (size_t)byte_count);
    for (int i = 0; i < byte_count; i++) {
        writer->bytes[writer->size++] = (u8)(value >> (8 * i));
    }
}

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

static inline void lha_put_end(ByteWriter* lha) {
    byte_put(lha, 0, 1);
}

// An archive holding one stored file.
static inline void lha_build(ByteWriter* lha, const char* name, const char* text) {
    lha_put_entry(lha, name, text, LHA_METHOD_STORED, LHA_ATTRIBUTE_FILE);
    lha_put_end(lha);
}

// The data is present and is exactly the expected bytes.
static inline bool vfs_bytes_equal(const void* data, i64 size, const void* expected, size_t expected_size) {
    return data != nullptr && (size_t)size == expected_size && memcmp(data, expected, expected_size) == 0;
}

// Prints what failed and returns ok, so a helper can gather several checks into one ASSERT_TRUE.
static inline bool vfs_check(bool ok, const char* what) {
    if (!ok) {
        printf("check failed: %s\n", what);
    }
    return ok;
}

#define VFS_AWAIT_FLAG_TIMEOUT_NS 20000000000ull
#define VFS_AWAIT_SHORT_NS 5000000000ull

typedef bool (*VfsPredicate)(const void* arg);

// Polls predicate until it holds or timeout_ns passes, and returns whether it held.
static inline bool vfs_await_until(VfsPredicate predicate, const void* arg, u64 timeout_ns) {
    u64 deadline_ns = fl_os_monotonic_ns() + timeout_ns;
    while (!predicate(arg)) {
        if (fl_os_monotonic_ns() >= deadline_ns) {
            return false;
        }
        sleep_ms(1);
    }
    return true;
}

static inline bool vfs_flag_is_set(const void* flag) {
    return atomic_load((const _Atomic bool*)flag);
}

static inline bool vfs_mount_is_ready_pred(const void* mount) {
    return fl_vfs_mount_is_ready((FlVfsMount*)mount);
}

static inline bool vfs_await_flag_for(_Atomic bool* flag, u64 timeout_ns) {
    return vfs_await_until(vfs_flag_is_set, flag, timeout_ns);
}

// Spins until flag is set or the deadline passes, and returns false on the deadline. Every wait on a flag that
// another thread sets goes through it, so a failed case reports and does not leave the only worker spinning.
static inline bool vfs_await_flag(_Atomic bool* flag) {
    return vfs_await_flag_for(flag, VFS_AWAIT_FLAG_TIMEOUT_NS);
}

// The plugin handles a fake driver has opened and closed since the watch began, read from its two counters.
typedef struct LeakWatch {
    const _Atomic int* alloc_counter;
    const _Atomic int* close_counter;
    int allocs_before;
    int closes_before;
} LeakWatch;

static inline LeakWatch leak_watch_begin(const _Atomic int* alloc_counter, const _Atomic int* close_counter) {
    return (LeakWatch) { .alloc_counter = alloc_counter,
                         .close_counter = close_counter,
                         .allocs_before = atomic_load(alloc_counter),
                         .closes_before = atomic_load(close_counter) };
}

static inline int leak_watch_allocated(const LeakWatch* watch) {
    return atomic_load(watch->alloc_counter) - watch->allocs_before;
}

static inline int leak_watch_closed(const LeakWatch* watch) {
    return atomic_load(watch->close_counter) - watch->closes_before;
}

static inline bool leak_watch_balanced(const LeakWatch* watch) {
    return vfs_check(leak_watch_closed(watch) == leak_watch_allocated(watch), "every plugin handle opened was closed");
}

// Exactly allocated handles were opened since the watch began, described by what, and all of them are closed.
static inline bool leak_watch_balanced_after(const LeakWatch* watch, int allocated, const char* what) {
    return vfs_check(leak_watch_allocated(watch) == allocated, what) && leak_watch_balanced(watch);
}

// What the counting fake archive drivers (Nest, Pfx, Ovr) advertise.
static inline FlVfsDriverCapabilities counting_fake_capabilities(void) {
    return (FlVfsDriverCapabilities)(FlVfsDriverCapabilities_Read | FlVfsDriverCapabilities_List
                                     | FlVfsDriverCapabilities_Memory);
}

// Matches a reconstructed path segment against a name, with or without a leading '/'.
static inline bool vfs_segment_is(FlString path, const char* name) {
    const char* d = path.data;
    int n = (int)path.length;
    if (n > 0 && d[0] == '/') {
        d++;
        n--;
    }
    int m = (int)strlen(name);
    return n == m && memcmp(d, name, (size_t)m) == 0;
}

// The singleton VFS with the stub driver registered, for a case that mounts "/stub".
static inline void stub_vfs_begin(void) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);
}

// Mounts source and waits for the mount job. nullptr unless the mount call succeeded.
static inline FlVfsMount* vfs_mount_started(FlString source) {
    FlVfsMountResult mount = vfs_mount(source);
    vfs_wait_all();
    return mount.status == FlVfsMountErrorStatus_Success ? mount.mount : nullptr;
}

// nullptr unless the mount call succeeded and the mount became ready.
static inline FlVfsMount* vfs_mount_ready(FlString source) {
    FlVfsMount* mount = vfs_mount_started(source);
    return mount != nullptr && fl_vfs_mount_is_ready(mount) ? mount : nullptr;
}

static inline FlVfsMount* stub_mount_started(void) {
    stub_vfs_begin();
    return vfs_mount_started(FL_S("/stub"));
}

static inline FlVfsMount* stub_mount_ready(void) {
    stub_vfs_begin();
    return vfs_mount_ready(FL_S("/stub"));
}

static inline FlVfsHandle stub_read_all(FlVfsMount* mount) {
    return fl_vfs_mount_read_all_with_options(mount, FL_S(STUB_VFS_FILE), nullptr, nullptr, nullptr,
                                              FL_VFS_HANDLE_INVALID);
}

// A read-all of the stub file succeeds and returns all of its bytes.
static inline bool stub_read_succeeds(FlVfsMount* mount, const char* what) {
    FlVfsHandle handle = stub_read_all(mount);
    if (handle == FL_VFS_HANDLE_INVALID) {
        return false;
    }
    fl_vfs_wait(handle);
    FlVfsData data = fl_vfs_get_data(handle);
    bool ok = vfs_check(data.success, what) && vfs_check((size_t)data.size == sizeof(k_stub_vfs_data), what);
    fl_vfs_close(handle);
    return ok;
}

// Arms the stub driver's read gate: the next read announces itself and blocks until the case releases it.
static inline void stub_read_gate_arm(void) {
    atomic_store(&s_stub_vfs_read_entered, false);
    atomic_store(&s_stub_vfs_read_release, false);
    atomic_store(&s_stub_vfs_read_gate_armed, true);
}

// unit_tests runs a one-worker job system. Parking that worker in a read-all callback keeps everything issued
// after it queued until the case lets it go.
typedef struct VfsParkCtx {
    _Atomic bool in_callback;
    _Atomic bool release;
} VfsParkCtx;

static inline FlVfsData vfs_park_callback(void* data, i64 size, void* user_data) {
    VfsParkCtx* ctx = (VfsParkCtx*)user_data;
    atomic_store(&ctx->in_callback, true);
    (void)vfs_await_flag(&ctx->release);
    return (FlVfsData) { .data = data, .size = size, .success = true };
}

// Parks the only worker in parker's callback, and returns false if it never got there. The caller stores
// ctx->release unconditionally before its first ASSERT. An ASSERT returns from the case, and a worker still
// parked then leaves vfs_destroy waiting on a spinning worker.
static inline bool vfs_park_worker(VfsParkCtx* ctx, FlVfsMount* mount, FlVfsHandle* parker, FlString path) {
    atomic_init(&ctx->in_callback, false);
    atomic_init(&ctx->release, false);
    *parker = vfs_mount_read_all_prepare(mount, path, vfs_park_callback, ctx, nullptr, FL_VFS_HANDLE_INVALID);
    if (*parker == FL_VFS_HANDLE_INVALID) {
        return false;
    }
    vfs_dispatch(*parker);
    return vfs_await_flag_for(&ctx->in_callback, VFS_AWAIT_SHORT_NS);
}

// A read-all's callback and release hook are counted separately, so a case can check when the VFS reaches each
// and that the release comes after the callback. The release tells user_data's owner the worker is done with it.
typedef struct VfsReleaseCtx {
    _Atomic int callback_calls;
    _Atomic int release_calls;
    _Atomic bool released_before_callback;
} VfsReleaseCtx;

static inline void vfs_release_ctx_reset(VfsReleaseCtx* ctx) {
    memset(ctx, 0, sizeof(*ctx));
    atomic_init(&ctx->callback_calls, 0);
    atomic_init(&ctx->release_calls, 0);
    atomic_init(&ctx->released_before_callback, false);
}

static inline FlVfsData vfs_count_callback(void* data, i64 size, void* user_data) {
    VfsReleaseCtx* ctx = (VfsReleaseCtx*)user_data;
    atomic_fetch_add(&ctx->callback_calls, 1);
    return (FlVfsData) { .data = data, .size = size, .success = true };
}

static inline void vfs_count_release(void* user_data) {
    VfsReleaseCtx* ctx = (VfsReleaseCtx*)user_data;
    if (atomic_load(&ctx->callback_calls) == 0) {
        atomic_store(&ctx->released_before_callback, true);
    }
    atomic_fetch_add(&ctx->release_calls, 1);
}

// The read ran callbacks times and released its user_data releases times.
static inline bool vfs_release_ctx_counts(const VfsReleaseCtx* ctx, int callbacks, int releases) {
    return vfs_check(atomic_load(&ctx->callback_calls) == callbacks, "the read's callback ran the expected times")
           && vfs_check(atomic_load(&ctx->release_calls) == releases, "user_data was released the expected times");
}

// As vfs_release_ctx_counts, and the release never came before the callback.
static inline bool vfs_release_ctx_is(const VfsReleaseCtx* ctx, int callbacks, int releases) {
    return vfs_release_ctx_counts(ctx, callbacks, releases)
           && vfs_check(!atomic_load(&ctx->released_before_callback), "the release must follow the callback");
}

// Starts from a pristine VFS with every archive driver this build contains registered.
static inline bool vfs_test_begin(void) {
    process_singletons_reset();
    if (!fl_vfs_init(s_arena)) {
        return false;
    }
    fl_vfs_register_builtin_drivers();
    return true;
}

// Mounts path and waits for the mount job. nullptr when the mount did not become ready.
static inline FlVfsMount* vfs_test_mount(FlString path) {
    FlVfsMount* mount = vfs_mount(path).mount;
    vfs_wait_all();
    return mount != nullptr && fl_vfs_mount_is_ready(mount) ? mount : nullptr;
}

// A fresh, empty directory under the working directory for a case to put files in. Empty when it cannot be made.
static inline FlString vfs_test_temp_dir(FlArena* arena, FlString name) {
    FlString dir = file_build_path_os(arena, get_current_directory_os(arena), name);
    file_remove_directory_recursive_os(dir);
    return file_create_directory_os(dir) ? dir : FL_S("");
}

// A vfs_test_temp_dir with the arena its paths live in; destroying it removes the directory and frees the arena.
typedef struct ScratchDir {
    FlArena* arena;
    FlString dir;
} ScratchDir;

// False when the directory cannot be made.
static inline bool scratch_dir_create(ScratchDir* scratch, FlString name) {
    scratch->arena = arena_new();
    scratch->dir = vfs_test_temp_dir(scratch->arena, name);
    return scratch->dir.length > 0;
}

static inline void scratch_dir_destroy(ScratchDir* scratch) {
    file_remove_directory_recursive_os(scratch->dir);
    fl_arena_destroy(scratch->arena);
}

static inline bool vfs_test_write_file(FlArena* arena, FlString dir, FlString name, const ByteWriter* bytes) {
    FlString path = file_build_path_os(arena, dir, name);
    return file_write_os(path, bytes->bytes, (i64)bytes->size) == (i64)bytes->size;
}

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

static inline FlVfsMount* vfs_mount_fixture(const char* name) {
    return vfs_test_mount(vfs_fixture_path(s_arena, string_from_cstr(name)));
}

static inline bool vfs_expect_reads(FlVfsMount* mount, const char* const* names, const char* const* contents,
                                    int count) {
    for (int i = 0; i < count; i++) {
        if (!vfs_expect_read(mount, string_from_cstr(names[i]), string_from_cstr(contents[i]))) {
            return false;
        }
    }
    return true;
}
