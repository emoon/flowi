// VFS nested archive tests with libarchive built in

#include "vfs_test_support.h"

// Real archives, with libarchive built in. The zips are written by hand (stored entries) so the cases need no
// fixture files. Archives are nested inside archives, and every path through any number of layers lists and reads
// through a mount of the directory, of the outer archive, or of a nested path directly.

#if FLOWI_VFS_DRIVER_LIBARCHIVE

#define ZIP_ENTRY_COUNT 4

static const char* const k_zip_names[ZIP_ENTRY_COUNT] = { "file1.txt", "file2.txt", "file3.txt", "file4.txt" };
static const char* const k_zip_contents[ZIP_ENTRY_COUNT]
    = { "Content 1\n", "Content 2\n", "Content 3\n", "Content 4\n" };

static void build_stored_zip(ByteWriter* zip) {
    ZipEntry entries[ZIP_ENTRY_COUNT];
    for (int i = 0; i < ZIP_ENTRY_COUNT; i++) {
        entries[i] = (ZipEntry) { k_zip_names[i], (const u8*)k_zip_contents[i], strlen(k_zip_contents[i]) };
    }
    zip_build(zip, entries, ZIP_ENTRY_COUNT);
}

#define STRESS_THREADS 4
#define STRESS_ITERATIONS 25

typedef struct ZipStressCtx {
    FlVfsMount* mount;
    _Atomic int failures;
} ZipStressCtx;

// Waits for the read of entry index and says whether it returned the entry's bytes.
static bool zip_entry_read_is_right(FlVfsHandle handle, int index) {
    fl_vfs_wait(handle);
    FlVfsData data = fl_vfs_get_data(handle);
    const bool ok = data.success && vfs_bytes_equal(data.data, data.size, k_zip_contents[index], strlen(k_zip_contents[index]));
    fl_vfs_close(handle);
    return ok;
}

// Reads every entry once through the mount and returns the number of reads that went wrong.
static int zip_read_round_failures(FlVfsMount* mount) {
    FlVfsHandle handles[ZIP_ENTRY_COUNT];
    for (int i = 0; i < ZIP_ENTRY_COUNT; i++) {
        handles[i] = vfs_mount_read_all(mount, string_from_cstr(k_zip_names[i]));
    }

    int failures = 0;
    for (int i = 0; i < ZIP_ENTRY_COUNT; i++) {
        failures += zip_entry_read_is_right(handles[i], i) ? 0 : 1;
    }
    return failures;
}

static void* zip_stress_thread(void* user_data) {
    ZipStressCtx* ctx = user_data;

    for (int iteration = 0; iteration < STRESS_ITERATIONS; iteration++) {
        atomic_fetch_add(&ctx->failures, zip_read_round_failures(ctx->mount));
    }

    return nullptr;
}

// Starts STRESS_THREADS readers on the archive mount and returns the number of reads that went wrong. -1 when a
// thread could not start.
static int zip_stress_failures(FlVfsMount* archive) {
    ZipStressCtx ctx = { .mount = archive };
    atomic_init(&ctx.failures, 0);
    Thread threads[STRESS_THREADS];
    int started = 0;
    while (started < STRESS_THREADS && os_thread_create(&threads[started], zip_stress_thread, &ctx)) {
        started++;
    }
    for (int i = 0; i < started; i++) {
        os_thread_join(&threads[i]);
    }
    return started == STRESS_THREADS ? atomic_load(&ctx.failures) : -1;
}

// The directory holding the archive reads through it too, as a plain path.
static bool directory_reads_into_the_archive(FlString dir) {
    FlVfsMount* directory = vfs_mount_started(dir);
    if (directory == nullptr) {
        return false;
    }
    FlVfsHandle through = vfs_mount_read_all(directory, FL_S("stress.zip/file1.txt"));
    fl_vfs_wait(through);
    FlVfsData through_data = fl_vfs_get_data(through);
    bool ok = vfs_check(through_data.success, "a directory mount resolves a path into an archive")
              && vfs_check((size_t)through_data.size == strlen(k_zip_contents[0]), "the entry's size");
    fl_vfs_close(through);

    FlVfsDirHandle listing = fl_vfs_get_listing(directory, FL_S(""), 0);
    fl_vfs_wait(listing);
    FlVfsFileList files = fl_vfs_get_file_list(listing);
    ok = ok && vfs_check(files.success && files.count == 1, "the directory lists the archive");
    fl_vfs_close(listing);

    fl_vfs_mount_close(directory);
    return ok;
}

UTEST(vfs_libarchive, concurrent_reads_through_one_archive_mount_return_the_right_bytes) {
    fl_vfs_init(s_arena); // idempotent
    fl_vfs_register_builtin_drivers();

    ScratchDir scratch;
    ASSERT_TRUE(scratch_dir_create(&scratch, FL_S("unit_test_vfs_zip_tmp")));
    FlString zip_path = file_build_path_os(scratch.arena, scratch.dir, FL_S("stress.zip"));

    ByteWriter* zip = byte_writer_create(scratch.arena);
    build_stored_zip(zip);
    ASSERT_EQ(file_write_os(zip_path, zip->bytes, (i64)zip->size), (i64)zip->size);

    FlVfsMount* archive = vfs_mount(zip_path).mount;
    ASSERT_TRUE(archive != nullptr);
    vfs_wait_all();
    ASSERT_TRUE_MSG(fl_vfs_mount_is_ready(archive), "the archive must mount by its own path");

    ASSERT_EQ(zip_stress_failures(archive), 0);
    ASSERT_TRUE(directory_reads_into_the_archive(scratch.dir));

    fl_vfs_mount_close(archive);
    scratch_dir_destroy(&scratch);
}

// Nested archives. The fixture directory holds
//
//   outer.zip   top.txt, sub/side.zip (hello.txt), mid.zip
//   mid.zip     mid.txt, sub/deep.txt, inner.zip
//   inner.zip   hello.txt
//   bad.zip     ok.txt, broken.zip (not an archive)
//   folder/lone.zip   hello.txt
//   corrupt.zip   not an archive
//   plain.txt

#define NESTED_INNER_HELLO "inner hello\n"
#define NESTED_SIDE_HELLO "side hello\n"
#define NESTED_DEEP "deep\n"
#define NESTED_MID "mid\n"
#define NESTED_TOP "top\n"
#define NESTED_LONE_HELLO "lone hello\n"
#define NESTED_OK "ok\n"

static bool nested_write_file(ScratchDir* fixture, FlString relative_path, const ByteWriter* zip) {
    return vfs_test_write_file(fixture->arena, fixture->dir, relative_path, zip);
}

static bool nested_fixture_create(ScratchDir* fixture, FlString dir_name) {
    if (!scratch_dir_create(fixture, dir_name)
        || !file_create_directory_os(file_build_path_os(fixture->arena, fixture->dir, FL_S("folder")))) {
        return false;
    }

    ByteWriter* inner = byte_writer_create(fixture->arena);
    zip_build(inner, (ZipEntry[]) { ZIP_TEXT("hello.txt", NESTED_INNER_HELLO) }, 1);

    ByteWriter* mid = byte_writer_create(fixture->arena);
    zip_build(mid,
              (ZipEntry[]) { ZIP_TEXT("mid.txt", NESTED_MID), ZIP_TEXT("sub/deep.txt", NESTED_DEEP),
                             ZIP_FILE("inner.zip", inner) },
              3);

    ByteWriter* side = byte_writer_create(fixture->arena);
    zip_build(side, (ZipEntry[]) { ZIP_TEXT("hello.txt", NESTED_SIDE_HELLO) }, 1);

    ByteWriter* outer = byte_writer_create(fixture->arena);
    zip_build(
        outer,
        (ZipEntry[]) { ZIP_TEXT("top.txt", NESTED_TOP), ZIP_FILE("sub/side.zip", side), ZIP_FILE("mid.zip", mid) }, 3);

    ByteWriter* lone = byte_writer_create(fixture->arena);
    zip_build(lone, (ZipEntry[]) { ZIP_TEXT("hello.txt", NESTED_LONE_HELLO) }, 1);

    ByteWriter* bad = byte_writer_create(fixture->arena);
    zip_build(bad,
              (ZipEntry[]) { ZIP_TEXT("ok.txt", NESTED_OK),
                             ZIP_TEXT("broken.zip", "PK this is not an archive, only named like one") },
              2);

    FlString plain = file_build_path_os(fixture->arena, fixture->dir, FL_S("plain.txt"));
    FlString corrupt = file_build_path_os(fixture->arena, fixture->dir, FL_S("corrupt.zip"));
    return file_write_os(corrupt, (const u8*)"PK this file is named like an archive", 37) == 37
           && nested_write_file(fixture, FL_S("outer.zip"), outer)
           && nested_write_file(fixture, FL_S("folder/lone.zip"), lone)
           && nested_write_file(fixture, FL_S("bad.zip"), bad) && file_write_os(plain, (const u8*)"plain\n", 6) == 6;
}

// Paths are prefix + name, where prefix is empty or ends in '/'.
static FlString nested_path(FlString prefix, const char* name) {
    return fl_sprintf_arena(s_arena, "%S%s", prefix, name);
}

static bool nested_expect_read(FlVfsMount* mount, FlString prefix, const char* name, const char* expected) {
    return vfs_expect_read(mount, nested_path(prefix, name), string_from_cstr(expected));
}

// The bytes of an archive file read as a file, which the tree must still serve after it has entered the archive.
static bool nested_expect_raw_archive(FlVfsMount* mount, const char* path) {
    FlVfsHandle handle = vfs_mount_read_all(mount, string_from_cstr(path));
    fl_vfs_wait(handle);
    FlVfsData data = fl_vfs_get_data(handle);
    bool ok = data.success && data.size > 4 && memcmp(data.data, "PK\x03\x04", 4) == 0;
    if (!ok) {
        printf("raw read of '%s' failed (success=%d size=%lld)\n", path, data.success, (long long)data.size);
    }
    fl_vfs_close(handle);
    return ok;
}

static bool nested_expect_read_fails(FlVfsMount* mount, FlString prefix, const char* name) {
    return vfs_expect_read_fails(mount, nested_path(prefix, name));
}

// name is the directory inside prefix ("" for prefix itself).
static bool nested_expect_list(FlVfsMount* mount, FlString prefix, const char* name, const char* expected) {
    FlString path = nested_path(prefix, name);
    if (path.length > 0 && path.data[path.length - 1] == '/') {
        path.length--;
    }
    return vfs_expect_list(mount, path, expected);
}

// Each level checks its own contents and then the level below it, so one call covers a whole chain. prefix is where
// the archive's root sits in the mount.

static bool nested_check_inner(FlVfsMount* mount, FlString prefix) {
    return nested_expect_list(mount, prefix, "", "hello.txt")
           && nested_expect_read(mount, prefix, "hello.txt", NESTED_INNER_HELLO);
}

static bool nested_check_mid(FlVfsMount* mount, FlString prefix) {
    return nested_expect_list(mount, prefix, "", "sub/ inner.zip* mid.txt")
           && nested_expect_list(mount, prefix, "sub", "deep.txt")
           && nested_expect_read(mount, prefix, "mid.txt", NESTED_MID)
           && nested_expect_read(mount, prefix, "sub/deep.txt", NESTED_DEEP)
           && nested_check_inner(mount, nested_path(prefix, "inner.zip/"));
}

static bool nested_check_outer_misses(FlVfsMount* mount, FlString prefix) {
    return nested_expect_read_fails(mount, prefix, "missing.txt")
           && nested_expect_read_fails(mount, prefix, "mid.zip/missing.txt")
           && nested_expect_read_fails(mount, prefix, "mid.zip/inner.zip/missing.txt")
           && nested_expect_read_fails(mount, prefix, "top.txt/nothing");
}

static bool nested_check_outer(FlVfsMount* mount, FlString prefix) {
    return nested_expect_list(mount, prefix, "", "sub/ mid.zip* top.txt")
           && nested_expect_list(mount, prefix, "sub", "side.zip*")
           && nested_expect_read(mount, prefix, "top.txt", NESTED_TOP)
           && nested_expect_list(mount, prefix, "sub/side.zip", "hello.txt")
           && nested_expect_read(mount, prefix, "sub/side.zip/hello.txt", NESTED_SIDE_HELLO)
           && nested_check_mid(mount, nested_path(prefix, "mid.zip/"))
           && nested_check_outer_misses(mount, prefix);
}

static bool nested_begin(ScratchDir* fixture, FlString dir_name) {
    return vfs_test_begin() && nested_fixture_create(fixture, dir_name);
}

static bool nested_check_raw_archives(FlVfsMount* mount) {
    return nested_expect_raw_archive(mount, "outer.zip") && nested_expect_raw_archive(mount, "outer.zip/mid.zip")
           && nested_expect_raw_archive(mount, "outer.zip/mid.zip/inner.zip")
           && nested_expect_raw_archive(mount, "outer.zip/sub/side.zip");
}

// A caller may also name the file by its absolute path, which the local driver accepts below any directory mount.
static bool nested_check_absolute_paths(FlVfsMount* mount, FlString dir) {
    FlString absolute = fl_sprintf_arena(s_arena, "%S/", dir);
    return nested_expect_read(mount, absolute, "plain.txt", "plain\n")
           && nested_expect_read(mount, absolute, "outer.zip/mid.zip/mid.txt", NESTED_MID)
           && nested_expect_list(mount, absolute, "outer.zip/mid.zip", "sub/ inner.zip* mid.txt");
}

static bool nested_check_directory_mount(FlVfsMount* mount, FlString dir) {
    return nested_expect_list(mount, FL_S(""), "", "folder/ bad.zip* corrupt.zip* outer.zip* plain.txt")
           && nested_expect_list(mount, FL_S(""), "folder", "lone.zip*")
           && nested_expect_read(mount, FL_S(""), "folder/lone.zip/hello.txt", NESTED_LONE_HELLO)
           && nested_check_outer(mount, FL_S("outer.zip/")) && nested_check_raw_archives(mount)
           && nested_check_absolute_paths(mount, dir);
}

UTEST(vfs_libarchive, nested_archives_list_and_read_through_a_directory_mount) {
    ScratchDir fixture;
    ASSERT_TRUE(nested_begin(&fixture, FL_S("unit_test_vfs_nested_dir_tmp")));

    FlVfsMount* mount = vfs_test_mount(fixture.dir);
    ASSERT_TRUE(mount != nullptr);
    ASSERT_TRUE(nested_check_directory_mount(mount, fixture.dir));

    fl_vfs_mount_close(mount);
    vfs_destroy();
    scratch_dir_destroy(&fixture);
}

UTEST(vfs_libarchive, nested_archives_list_and_read_through_a_mount_of_the_outer_archive) {
    ScratchDir fixture;
    ASSERT_TRUE(nested_begin(&fixture, FL_S("unit_test_vfs_nested_outer_tmp")));

    FlVfsMount* mount = vfs_test_mount(file_build_path_os(fixture.arena, fixture.dir, FL_S("outer.zip")));
    ASSERT_TRUE(mount != nullptr);
    ASSERT_TRUE(nested_check_outer(mount, FL_S("")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
    scratch_dir_destroy(&fixture);
}

UTEST(vfs_libarchive, a_nested_path_mounts_directly_and_resolves_further_layers_below_it) {
    ScratchDir fixture;
    ASSERT_TRUE(nested_begin(&fixture, FL_S("unit_test_vfs_nested_direct_tmp")));

    FlString outer = file_build_path_os(fixture.arena, fixture.dir, FL_S("outer.zip"));

    FlVfsMount* mid = vfs_test_mount(fl_sprintf_arena(fixture.arena, "%S/mid.zip", outer));
    ASSERT_TRUE_MSG(mid != nullptr, "outer.zip/mid.zip must mount");
    ASSERT_TRUE(nested_check_mid(mid, FL_S("")));

    FlVfsMount* inner = vfs_test_mount(fl_sprintf_arena(fixture.arena, "%S/mid.zip/inner.zip", outer));
    ASSERT_TRUE_MSG(inner != nullptr, "outer.zip/mid.zip/inner.zip must mount");
    ASSERT_TRUE(nested_check_inner(inner, FL_S("")));

    FlVfsMount* side = vfs_test_mount(fl_sprintf_arena(fixture.arena, "%S/sub/side.zip", outer));
    ASSERT_TRUE_MSG(side != nullptr, "an archive inside a subdirectory of an archive must mount");
    ASSERT_TRUE(nested_expect_list(side, FL_S(""), "", "hello.txt"));
    ASSERT_TRUE(nested_expect_read(side, FL_S(""), "hello.txt", NESTED_SIDE_HELLO));

    fl_vfs_mount_close(mid);
    fl_vfs_mount_close(inner);
    fl_vfs_mount_close(side);
    vfs_destroy();
    scratch_dir_destroy(&fixture);
}

UTEST(vfs_libarchive, a_nested_file_opens_and_reads_through_a_chained_read) {
    ScratchDir fixture;
    ASSERT_TRUE(nested_begin(&fixture, FL_S("unit_test_vfs_nested_open_tmp")));

    FlVfsMount* mount = vfs_test_mount(fixture.dir);
    ASSERT_TRUE(mount != nullptr);

    FlVfsHandle file = fl_vfs_mount_open(mount, FL_S("outer.zip/mid.zip/inner.zip/hello.txt"), FlVfsOpenFlags_Read);
    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);
    u8 buffer[64] = { 0 };
    FlVfsHandle read = fl_vfs_read(file, buffer, sizeof(buffer));
    ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(read);

    FlVfsData data = fl_vfs_get_data(read);
    ASSERT_TRUE(data.success);
    ASSERT_EQ((size_t)data.size, strlen(NESTED_INNER_HELLO));
    ASSERT_EQ(memcmp(buffer, NESTED_INNER_HELLO, strlen(NESTED_INNER_HELLO)), 0);

    fl_vfs_close(read);
    fl_vfs_close(file);
    fl_vfs_mount_close(mount);
    vfs_destroy();
    scratch_dir_destroy(&fixture);
}

// Threads reading and listing at different depths through one mount, while the archives are opened for the first time.

#define NESTED_THREADS 6
#define NESTED_ROUNDS 4
#define NESTED_PATHS 5

static const char* const k_nested_paths[NESTED_PATHS] = {
    "outer.zip/top.txt",
    "outer.zip/sub/side.zip/hello.txt",
    "outer.zip/mid.zip/mid.txt",
    "outer.zip/mid.zip/sub/deep.txt",
    "outer.zip/mid.zip/inner.zip/hello.txt",
};
static const char* const k_nested_contents[NESTED_PATHS]
    = { NESTED_TOP, NESTED_SIDE_HELLO, NESTED_MID, NESTED_DEEP, NESTED_INNER_HELLO };

#define NESTED_LISTINGS 3

static const char* const k_nested_listings[NESTED_LISTINGS]
    = { "outer.zip/mid.zip", "outer.zip/mid.zip/inner.zip", "outer.zip/sub/side.zip" };

// The unit_tests job system has one worker, so jobs never overlap. The ops are built by hand and run from several
// threads at once, as operations_on_one_mount_never_overlap_inside_its_driver does, with a new mount per round so
// that the first opens of every archive race inside the tree.

typedef struct NestedRaceCtx {
    VfsOp* reads[NESTED_PATHS];
    FlVfsHandle handles[NESTED_PATHS];
    int order[NESTED_PATHS];
    VfsOp lists[NESTED_LISTINGS];
} NestedRaceCtx;

static void* nested_race_thread(void* user_data) {
    NestedRaceCtx* ctx = user_data;
    for (int i = 0; i < NESTED_PATHS; i++) {
        vfs_ops_execute_sync(ctx->reads[i]);
        if (i < NESTED_LISTINGS) {
            vfs_ops_execute_sync(&ctx->lists[i]);
        }
    }
    return nullptr;
}

// Thread t reads every path and lists every directory, each starting at a different place.
static void nested_race_prepare(NestedRaceCtx* ctx, FlVfsMount* mount, int t) {
    for (int i = 0; i < NESTED_PATHS; i++) {
        int p = (t + i) % NESTED_PATHS;
        ctx->order[i] = p;
        ctx->handles[i] = vfs_mount_read_all_prepare(mount, string_from_cstr(k_nested_paths[p]), nullptr, nullptr,
                                                     nullptr, FL_VFS_HANDLE_INVALID);
        ctx->reads[i] = vfs_op_acquire(ctx->handles[i]);
    }
    for (int i = 0; i < NESTED_LISTINGS; i++) {
        int l = (t + i) % NESTED_LISTINGS;
        ctx->lists[i].mount = mount;
        ctx->lists[i].op_type = VfsOp_MountList;
        ctx->lists[i].path = string_from_cstr(k_nested_listings[l]);
    }
}

// Runs every thread's ops at once. False when a thread could not start.
static bool nested_race_run(NestedRaceCtx* ctx) {
    Thread threads[NESTED_THREADS];
    int started = 0;
    while (started < NESTED_THREADS && os_thread_create(&threads[started], nested_race_thread, &ctx[started])) {
        started++;
    }
    for (int t = 0; t < started; t++) {
        os_thread_join(&threads[t]);
    }
    return started == NESTED_THREADS;
}

// Reads of one thread's ops that returned the wrong bytes. The ops are released here.
static int nested_race_read_failures(NestedRaceCtx* ctx) {
    int failures = 0;
    for (int i = 0; i < NESTED_PATHS; i++) {
        FlVfsData data = fl_vfs_get_data(ctx->handles[i]);
        const char* expected = k_nested_contents[ctx->order[i]];
        failures += data.success && vfs_bytes_equal(data.data, data.size, expected, strlen(expected)) ? 0 : 1;
        vfs_op_release(ctx->reads[i]);
        fl_vfs_close(ctx->handles[i]);
    }
    return failures;
}

// Listings of one thread's ops that failed. The listings are released here.
static int nested_race_list_failures(NestedRaceCtx* ctx) {
    int failures = 0;
    for (int i = 0; i < NESTED_LISTINGS; i++) {
        failures += atomic_load(&ctx->lists[i].error_status) == VFS_STATE_SUCCESS ? 0 : 1;
        fl_arena_destroy(ctx->lists[i].result_arena);
    }
    return failures;
}

// One round on a fresh mount. Returns false when the threads could not start, and adds to failures otherwise.
static bool nested_race_round(FlVfsMount* mount, int* failures) {
    NestedRaceCtx ctx[NESTED_THREADS];
    memset(ctx, 0, sizeof(ctx));
    for (int t = 0; t < NESTED_THREADS; t++) {
        nested_race_prepare(&ctx[t], mount, t);
    }
    const bool ran = nested_race_run(ctx);
    for (int t = 0; ran && t < NESTED_THREADS; t++) {
        *failures += nested_race_read_failures(&ctx[t]) + nested_race_list_failures(&ctx[t]);
    }
    return ran;
}

UTEST(vfs_libarchive, first_opens_of_nested_archives_race_inside_the_tree) {
    ScratchDir fixture;
    ASSERT_TRUE(nested_begin(&fixture, FL_S("unit_test_vfs_nested_race_tmp")));
    int failures = 0;

    for (int round = 0; round < NESTED_ROUNDS; round++) {
        FlVfsMount* mount = vfs_test_mount(fixture.dir);
        ASSERT_TRUE(mount != nullptr);
        ASSERT_TRUE(nested_race_round(mount, &failures));
        fl_vfs_mount_close(mount);
    }
    ASSERT_EQ(failures, 0);
    vfs_destroy();
    scratch_dir_destroy(&fixture);
}

// libarchive wrapped so the test can count the handles it hands out and takes back.

static FlVfsPlugin s_counting_archive_plugin;
static _Atomic int s_archive_opened;
static _Atomic int s_archive_closed;
static _Atomic int s_archive_from_file;   // archives opened in place from a file on disk
static _Atomic int s_archive_from_memory; // archives opened from a copy in memory

static FlVfsOpenResult counting_archive_open(void* instance, void* parent, FlString path, uint32_t flags,
                                             FlVfsMountOptions options) {
    FlVfsOpenResult result = vfs_driver_libarchive()->open(instance, parent, path, flags, options);
    if (result.handle) {
        atomic_fetch_add(&s_archive_opened, 1);
        if (!parent) {
            atomic_fetch_add(&s_archive_from_file, 1);
        }
    }
    return result;
}

static FlVfsOpenResult counting_archive_open_memory(void* instance, const uint8_t* data, uint64_t size,
                                                    FlString original_path) {
    FlVfsOpenResult result = vfs_driver_libarchive()->open_memory(instance, data, size, original_path);
    if (result.handle) {
        atomic_fetch_add(&s_archive_opened, 1);
        atomic_fetch_add(&s_archive_from_memory, 1);
    }
    return result;
}

static void counting_archive_close(void* instance, void* handle) {
    atomic_fetch_add(&s_archive_closed, 1);
    vfs_driver_libarchive()->close(instance, handle);
}

// Starts a new VFS whose libarchive is the counting wrapper below, and creates the fixture directory.
static bool nested_begin_counting(ScratchDir* fixture, FlString dir_name) {
    process_singletons_reset();
    if (!fl_vfs_init(s_arena) || !nested_fixture_create(fixture, dir_name)) {
        return false;
    }

    s_counting_archive_plugin = *vfs_driver_libarchive();
    s_counting_archive_plugin.open = counting_archive_open;
    s_counting_archive_plugin.open_memory = counting_archive_open_memory;
    s_counting_archive_plugin.close = counting_archive_close;
    vfs_test_register_driver(&s_counting_archive_plugin);
    atomic_store(&s_archive_opened, 0);
    atomic_store(&s_archive_closed, 0);
    atomic_store(&s_archive_from_file, 0);
    atomic_store(&s_archive_from_memory, 0);
    return true;
}

// Every archive the wrapper handed out was taken back, and at least one was handed out.
static bool counting_archives_were_used_and_all_closed(void) {
    return vfs_check(atomic_load(&s_archive_opened) > 0, "an archive was opened")
           && vfs_check(atomic_load(&s_archive_closed) == atomic_load(&s_archive_opened),
                        "every archive layer that was opened is closed");
}

// A corrupt archive on disk is tried in place, then from memory, and fails both ways without a handle.
static bool corrupt_archive_on_disk_opens_no_handle(FlVfsMount* mount) {
    const int opened_before_corrupt_file = atomic_load(&s_archive_opened);
    return nested_expect_read_fails(mount, FL_S("corrupt.zip/"), "x.txt")
           && vfs_check(atomic_load(&s_archive_opened) == opened_before_corrupt_file,
                        "a corrupt archive on disk opens no handle");
}

static bool corrupt_inner_archive_lists_nothing(FlVfsMount* mount) {
    FlVfsDirHandle listing = fl_vfs_get_listing(mount, FL_S("bad.zip/broken.zip"), 0);
    fl_vfs_wait(listing);
    FlVfsFileList files = fl_vfs_get_file_list(listing);
    const bool ok = files.success && vfs_check(files.count == 0, "a corrupt archive is not entered, so nothing is listed in it");
    fl_vfs_close(listing);
    return ok;
}

static bool corrupt_archives_fail_cleanly(FlVfsMount* mount) {
    if (!nested_expect_read(mount, FL_S("bad.zip/"), "ok.txt", NESTED_OK)) {
        return false;
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        if (!nested_expect_read_fails(mount, FL_S("bad.zip/"), "broken.zip/x.txt")) {
            return false;
        }
    }
    return corrupt_inner_archive_lists_nothing(mount) && corrupt_archive_on_disk_opens_no_handle(mount);
}

UTEST(vfs_libarchive, a_corrupt_inner_archive_fails_cleanly_and_leaks_no_handle) {
    ScratchDir fixture;
    ASSERT_TRUE(nested_begin_counting(&fixture, FL_S("unit_test_vfs_nested_corrupt_tmp")));

    FlVfsMount* mount = vfs_test_mount(fixture.dir);
    ASSERT_TRUE(mount != nullptr);
    ASSERT_TRUE(corrupt_archives_fail_cleanly(mount));
    ASSERT_TRUE(nested_expect_read(mount, FL_S("outer.zip/mid.zip/"), "mid.txt", NESTED_MID));

    fl_vfs_mount_close(mount);
    ASSERT_TRUE(counting_archives_were_used_and_all_closed());

    vfs_destroy();
    scratch_dir_destroy(&fixture);
}

typedef struct NestedCloseCtx {
    _Atomic int good;
    _Atomic int calls;
} NestedCloseCtx;

static FlVfsData nested_close_callback(void* data, i64 size, void* user_data) {
    NestedCloseCtx* ctx = user_data;
    atomic_fetch_add(&ctx->calls, 1);
    if (vfs_bytes_equal(data, size, NESTED_INNER_HELLO, strlen(NESTED_INNER_HELLO))) {
        atomic_fetch_add(&ctx->good, 1);
    }
    return (FlVfsData) { .data = data, .size = size, .success = data != nullptr };
}

static bool every_queued_read_ran_with_the_right_bytes(const NestedCloseCtx* ctx) {
    return vfs_check(atomic_load(&ctx->calls) == 8, "every queued read ran before the mount went")
           && vfs_check(atomic_load(&ctx->good) == 8, "and read the right bytes through the archives");
}

UTEST(vfs_libarchive, closing_a_mount_with_reads_in_flight_through_archives_finishes_them_and_closes_every_archive) {
    ScratchDir fixture;
    ASSERT_TRUE(nested_begin_counting(&fixture, FL_S("unit_test_vfs_nested_close_tmp")));

    FlVfsMount* mount = vfs_test_mount(fixture.dir);
    ASSERT_TRUE(mount != nullptr);

    static NestedCloseCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    for (int i = 0; i < 8; i++) {
        FlVfsHandle read = vfs_mount_read_all(mount, FL_S("outer.zip/mid.zip/inner.zip/hello.txt"),
                                              .callback = nested_close_callback, .user_data = &ctx);
        ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    }
    FlVfsHandle file = fl_vfs_mount_open(mount, FL_S("outer.zip/sub/side.zip/hello.txt"), FlVfsOpenFlags_Read);
    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);

    fl_vfs_mount_close(mount);

    ASSERT_TRUE(every_queued_read_ran_with_the_right_bytes(&ctx));
    ASSERT_TRUE(counting_archives_were_used_and_all_closed());

    vfs_destroy();
    scratch_dir_destroy(&fixture);
}

static bool first_read_opened_each_archive_once(int opened_by_first_read) {
    return vfs_check(opened_by_first_read == 4, "three archives, plus the file handle the read opens inside the last")
           && vfs_check(atomic_load(&s_archive_from_file) == 1, "outer.zip is opened in place from its file on disk")
           && vfs_check(atomic_load(&s_archive_from_memory) == 2, "mid.zip and inner.zip are opened from memory");
}

UTEST(vfs_libarchive, a_nested_archive_is_opened_once_and_kept_for_the_mount) {
    ScratchDir fixture;
    ASSERT_TRUE(nested_begin_counting(&fixture, FL_S("unit_test_vfs_nested_once_tmp")));

    FlVfsMount* mount = vfs_test_mount(fixture.dir);
    ASSERT_TRUE(mount != nullptr);

    ASSERT_TRUE(nested_expect_read(mount, FL_S("outer.zip/mid.zip/inner.zip/"), "hello.txt", NESTED_INNER_HELLO));
    const int opened_by_first_read = atomic_load(&s_archive_opened);
    ASSERT_TRUE(first_read_opened_each_archive_once(opened_by_first_read));

    ASSERT_TRUE(nested_expect_read(mount, FL_S("outer.zip/mid.zip/inner.zip/"), "hello.txt", NESTED_INNER_HELLO));
    ASSERT_TRUE(nested_expect_list(mount, FL_S("outer.zip/mid.zip/"), "inner.zip", "hello.txt"));
    ASSERT_EQ_MSG(atomic_load(&s_archive_opened) - opened_by_first_read, 1,
                  "the archives on the path stay open: only the file handle of the second read is new");

    fl_vfs_mount_close(mount);
    ASSERT_EQ(atomic_load(&s_archive_closed), atomic_load(&s_archive_opened));

    vfs_destroy();
    scratch_dir_destroy(&fixture);
}

// An LHA archive (stored, one file), alone on disk and inside a zip: archives of different drivers nest.

#if FLOWI_VFS_DRIVER_LHASA

static bool lha_fixture_write(ScratchDir* fixture) {
    ByteWriter* lha = byte_writer_create(fixture->arena);
    lha_build(lha, "song.txt", "lha song\n");
    ByteWriter* wrapper = byte_writer_create(fixture->arena);
    zip_build(wrapper, (ZipEntry[]) { ZIP_FILE("pack.lha", lha) }, 1);
    return nested_write_file(fixture, FL_S("wrapper.zip"), wrapper) && nested_write_file(fixture, FL_S("disk.lha"), lha);
}

static bool lha_nested_in_a_zip_and_on_disk_reads(FlVfsMount* mount) {
    return nested_expect_list(mount, FL_S(""), "wrapper.zip", "pack.lha*")
           && nested_expect_list(mount, FL_S(""), "wrapper.zip/pack.lha", "song.txt")
           && nested_expect_read(mount, FL_S(""), "wrapper.zip/pack.lha/song.txt", "lha song\n")
           && nested_expect_list(mount, FL_S(""), "disk.lha", "song.txt")
           && nested_expect_read(mount, FL_S(""), "disk.lha/song.txt", "lha song\n");
}

UTEST(vfs_lhasa, an_lha_archive_nests_in_a_zip_and_sits_on_disk) {
    ScratchDir fixture;
    ASSERT_TRUE(nested_begin(&fixture, FL_S("unit_test_vfs_nested_lha_tmp")));
    ASSERT_TRUE(lha_fixture_write(&fixture));

    FlVfsMount* mount = vfs_test_mount(fixture.dir);
    ASSERT_TRUE(mount != nullptr);
    ASSERT_TRUE(lha_nested_in_a_zip_and_on_disk_reads(mount));

    fl_vfs_mount_close(mount);
    vfs_destroy();
    scratch_dir_destroy(&fixture);
}

#endif // FLOWI_VFS_DRIVER_LHASA

#endif // FLOWI_VFS_DRIVER_LIBARCHIVE
