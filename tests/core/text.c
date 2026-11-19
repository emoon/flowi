// sprintf, string, env expansion and hash tests

#include "unit_tests_common.h"
#include "scoped_env.h"

#define SPRINTF_MT_THREADS 4

#define SPRINTF_MT_ITERS 128

// Concurrent formatting of messages longer than one stb chunk (about 512 chars) on a shared FlArenaMt returns a
// contiguous FlString that holds only its own bytes. Each worker formats one fill character per thread, so a
// foreign byte between the internal 512-char chunks shows at any offset.

typedef struct SprintfMtWorker {
    FlArenaMt* arena;
    int id;
    bool ok;
} SprintfMtWorker;

static bool sprintf_mt_format_is_contiguous(FlArenaMt* arena, char fill, int len) {
    char expected[1024];
    memset(expected, fill, (size_t)len);
    expected[len] = 0;

    FlString s = sprintf_arena_mt(arena, "%s", expected);
    return (int)s.length == len && memcmp(s.data, expected, (size_t)len) == 0;
}

static void* sprintf_mt_worker(void* user) {
    SprintfMtWorker* w = (SprintfMtWorker*)user;
    const char fill = (char)('A' + w->id);

    for (int i = 0; i < SPRINTF_MT_ITERS; ++i) {
        const int len = 600 + (i % 300); // always over 512, so it spans more than one stb chunk
        if (!sprintf_mt_format_is_contiguous(w->arena, fill, len)) {
            w->ok = false;
            return nullptr;
        }
    }

    w->ok = true;
    return nullptr;
}

UTEST(sprintf, arena_mt_concurrent_contiguous) {
    FlArenaMt* arena = arena_mt_new();

    SprintfMtWorker workers[SPRINTF_MT_THREADS];
    Thread threads[SPRINTF_MT_THREADS];
    for (int t = 0; t < SPRINTF_MT_THREADS; ++t) {
        workers[t] = (SprintfMtWorker) { .arena = arena, .id = t, .ok = false };
        ASSERT_TRUE(os_thread_create(&threads[t], sprintf_mt_worker, &workers[t]));
    }

    for (int t = 0; t < SPRINTF_MT_THREADS; ++t) {
        os_thread_join(&threads[t]);
        ASSERT_TRUE_MSG(workers[t].ok, "a worker saw a corrupt/non-contiguous sprintf_arena_mt result");
    }

    arena_mt_destroy(arena);
}

// stbsp_snprintf with count == 0 and a non-null buffer measures the required length: it writes nothing and
// returns the full formatted length. A sentinel byte just before the buffer catches a write one byte too early.

extern int stbsp_snprintf(char* buf, int count, char const* fmt, ...);

UTEST(sprintf, snprintf_count_zero_writes_nothing) {
    char guarded[2] = { 0x5A, 0x5A };

    const int len = stbsp_snprintf(&guarded[1], 0, "hello %d", 1234);

    ASSERT_EQ(len, 10);          // "hello 1234"
    ASSERT_EQ(guarded[0], 0x5A); // byte before buf untouched
    ASSERT_EQ(guarded[1], 0x5A); // buf itself untouched
}

UTEST(sprintf, snprintf_positive_count_still_truncates_and_terminates) {
    char buf[6];
    memset(buf, 0x5A, sizeof buf);

    const int len = stbsp_snprintf(buf, 4, "hello %d", 1234);

    ASSERT_EQ(len, 10);       // the full length, not clamped to count
    ASSERT_STREQ(buf, "hel"); // 3 chars + terminator within count == 4
    ASSERT_EQ(buf[4], 0x5A);  // nothing written past count
}

// A FlString with null data formatted with %S prints the literal "null", whatever length it carries.
UTEST(sprintf, format_S_null_data_ignores_stale_length) {
    char buf[32];
    FlString bogus = (FlString) { .data = nullptr, .length = 100 };

    const int len = stbsp_snprintf(buf, sizeof buf, "%S", bogus);

    ASSERT_EQ(len, 4);
    ASSERT_STREQ(buf, "null");
}

// An unrecognized %v vector spec is echoed literally, with every digit in its original order.
UTEST(sprintf, unknown_vector_format_echoes_literally) {
    char buf[64];

    int len = stbsp_snprintf(buf, sizeof buf, "%v3f");
    ASSERT_EQ(len, 3);
    ASSERT_STREQ(buf, "v3f");

    len = stbsp_snprintf(buf, sizeof buf, "%v10x");
    ASSERT_EQ(len, 4);
    ASSERT_STREQ(buf, "v10x");

    len = stbsp_snprintf(buf, sizeof buf, "%vq");
    ASSERT_EQ(len, 2);
    ASSERT_STREQ(buf, "vq");
}

// Puts the fallback's writes across the boundary of the 512-byte internal callback buffer. The bounds checks
// flush before writing past it.
UTEST(sprintf, unknown_vector_format_at_callback_boundary) {
    char fmt[532];
    memset(fmt, 'x', 510);
    memcpy(fmt + 510, "%v10z", 6);

    char out[1024];
    const int len = stbsp_snprintf(out, sizeof out, fmt);

    ASSERT_EQ(len, 514);
    ASSERT_STREQ(out + 510, "v10z");
}

// strtoull accepts a leading sign and wraps negatives into huge unsigned values. fl_string_parse_u64 rejects
// signed input as InvalidFormat. Valid unsigned parsing and overflow detection are unaffected.

UTEST(string, parse_u64_rejects_signed_input) {
    static const char* rejected[] = { "-5", "-0", "+5", " -5", "\t-9223372036854775808", "-18446744073709551615" };
    for (size_t i = 0; i < sizeof_array(rejected); ++i) {
        StringParseResultU64 r = fl_string_parse_u64(string_from_cstr(rejected[i]));
        EXPECT_EQ_MSG((int)r.status, (int)StringParseStatus_InvalidFormat, rejected[i]);
        EXPECT_EQ(r.value, (u64)0);
    }

    StringParseResultU64 ok = fl_string_parse_u64(string_from_cstr("18446744073709551615"));
    EXPECT_EQ((int)ok.status, (int)StringParseStatus_Success);
    EXPECT_EQ(ok.value, UINT64_MAX);

    StringParseResultU64 zero = fl_string_parse_u64(string_from_cstr("0"));
    EXPECT_EQ((int)zero.status, (int)StringParseStatus_Success);
    EXPECT_EQ(zero.value, (u64)0);

    // UINT64_MAX + 1.
    StringParseResultU64 over = fl_string_parse_u64(string_from_cstr("18446744073709551616"));
    EXPECT_EQ((int)over.status, (int)StringParseStatus_Overflow);
}

// fl_string_compare_n: when max_len is below both strings' lengths, equal bytes within the bound compare as 0.
// The length tie-break (shorter sorts first) applies only when the full common prefix was compared.

static int sign_of(int value) {
    return (value > 0) - (value < 0);
}

UTEST(string, compare_n_clamped_equal_prefix_is_equal) {
    FlString a = string_from_cstr("hello");
    FlString b = string_from_cstr("hello_world");
    FlString c = string_from_cstr("hello_there");

    const struct {
        FlString lhs;
        FlString rhs;
        u64 max_len;
        int expected_sign;
    } cases[] = {
        // max_len below both lengths, identical bytes inside the bound: equal both ways.
        { b, c, 6, 0 },
        { c, b, 6, 0 },
        { b, c, 7, 1 }, // 'w' > 't' once the differing byte is inside the bound
        // max_len at or above both lengths: the length tie-break applies and the shorter string sorts first.
        { a, b, 64, -1 },
        { b, a, 64, 1 },
        // max_len == 0: everything is equal within an empty bound.
        { a, b, 0, 0 },
        // max_len == min(a.length, b.length) is not clamped. The full common prefix was compared, so the tie-break
        // applies, as it does in the zero-length case.
        { a, b, 5, -1 },
        { a, string_empty(), 0, 1 },
        { string_empty(), string_empty(), 64, 0 },
    };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        EXPECT_EQ(sign_of(fl_string_compare_n(cases[i].lhs, cases[i].rhs, cases[i].max_len)), cases[i].expected_sign);
    }
}

UTEST(string, from_cstr_public_constructor) {
    FlString value = string_from_cstr("hello");
    EXPECT_EQ((uint64_t)value.length, 5);
    EXPECT_EQ(value.data, "hello");
    EXPECT_FALSE(value.is_static);

    FlString empty = string_from_cstr(nullptr);
    EXPECT_EQ((uint64_t)empty.length, 0);
    EXPECT_EQ(empty.data, nullptr);
}

UTEST(string, short_literal_macros_match_the_fl_forms) {
    const FlString short_form = S("héllo");
    const FlString long_form = FL_S("héllo");
    EXPECT_TRUE(fl_string_equals(short_form, long_form));
    EXPECT_TRUE(short_form.is_static);
    EXPECT_FALSE(S_UTF8("héllo").is_ascii);
    const FlString braced = S_("abc");
    EXPECT_EQ((uint64_t)braced.length, 3);
}

// fl_string_to_cstr_buffer with buffer_len == 0 has no room even for the null terminator and writes nothing to
// dest. The unsigned buffer_len - 1 must not underflow into copying everything.

UTEST(string, to_cstr_buffer_zero_buffer_len_writes_nothing) {
    // Any write into the zero-sized destination changes the sentinel bytes.
    char sentinel[4] = { 'A', 'B', 'C', 'D' };
    fl_string_to_cstr_buffer(sentinel, 0, string_from_cstr("hello"));
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(sentinel[i], "ABCD"[i]);
    }

    char one[2] = { 'X', 'Y' };
    fl_string_to_cstr_buffer(one, 1, string_from_cstr("hello"));
    EXPECT_EQ(one[0], '\0');
    EXPECT_EQ(one[1], 'Y'); // byte past the declared buffer size untouched

    char small[4];
    fl_string_to_cstr_buffer(small, sizeof(small), string_from_cstr("hello"));
    EXPECT_STREQ(small, "hel");

    char big[16];
    fl_string_to_cstr_buffer(big, sizeof(big), string_from_cstr("hello"));
    EXPECT_STREQ(big, "hello");
}

// expand_env_vars needs a temporary allocation per variable for the getenv() name lookup. That scratch has to be
// separate from the caller's arena: when the caller passes the thread-local scratch arena, the plain scratch
// call returns that same arena and the temp scope's rewind frees the appended bytes.

UTEST(env_expand, output_on_scratch_arena_survives_temp_rewind) {
    SCOPED_ENV(env_a, "FLOWI_TEST_EXPAND_A", "alpha");
    SCOPED_ENV(env_b, "FLOWI_TEST_EXPAND_B", "beta");

    // The output arena is the thread-local scratch arena, which is the aliasing case.
    arena_scratch_auto(scratch);
    FlString result = expand_env_vars(scratch.arena, FL_S("${FLOWI_TEST_EXPAND_A}/mid/$FLOWI_TEST_EXPAND_B-end"));
    EXPECT_TRUE(fl_string_equals(result, FL_S("alpha/mid/beta-end")));

    // Undefined variables are kept verbatim, also with the aliased arena.
    FlString kept = expand_env_vars(scratch.arena, FL_S("${FLOWI_TEST_EXPAND_MISSING}/tail"));
    EXPECT_TRUE(fl_string_equals(kept, FL_S("${FLOWI_TEST_EXPAND_MISSING}/tail")));
}

// fl_hash_sha1 with nullptr data and zero length hashes the empty message and writes the SHA1("") digest. An
// untouched output buffer would have callers read uninitialized memory.

UTEST(hash, sha1_null_empty_input_is_empty_string_digest) {
    // SHA1("") = da39a3ee5e6b4b0d3255bfef95601890afd80709
    static const u8 empty_digest[20] = { 0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
                                         0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09 };

    u8 digest[20];
    memset(digest, 0xab, sizeof(digest)); // so an untouched buffer fails the comparison
    fl_hash_sha1(nullptr, 0, digest);
    EXPECT_EQ(memcmp(digest, empty_digest, sizeof(digest)), 0);

    // Empty input with a non-null pointer gives the same digest.
    u8 digest2[20];
    memset(digest2, 0xcd, sizeof(digest2));
    fl_hash_sha1((const u8*)"", 0, digest2);
    EXPECT_EQ(memcmp(digest2, empty_digest, sizeof(digest2)), 0);

    char hex[41];
    memset(hex, 0xef, sizeof(hex));
    hash_sha1_hex(nullptr, 0, hex);
    EXPECT_STREQ(hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

UTEST(hash, sha1_known_answer_vectors) {
    // FIPS 180 vector: SHA1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
    char hex[41];
    hash_sha1_hex((const u8*)"abc", 3, hex);
    EXPECT_STREQ(hex, "a9993e364706816aba3e25717850c26c9cd0d89d");

    // FIPS 180 vector of 56 bytes. The message plus padding spans two 512-bit blocks.
    const char* msg2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    hash_sha1_hex((const u8*)msg2, 56, hex);
    EXPECT_STREQ(hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    // 128 'a' bytes, more than one block, so the full-chunk loop runs before the tail.
    u8 msg3[128];
    memset(msg3, 'a', sizeof(msg3));
    hash_sha1_hex(msg3, sizeof(msg3), hex);
    EXPECT_STREQ(hex, "ad5b3fdbcb526778c2839d2f151ea753995e26a0");

    // The binary digest matches the hex one.
    static const u8 abc_digest[20] = { 0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                                       0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d };
    u8 digest[20];
    fl_hash_sha1((const u8*)"abc", 3, digest);
    EXPECT_EQ(memcmp(digest, abc_digest, sizeof(digest)), 0);
}

// fl_hash_sha1 does its chunk-count and tail-padding arithmetic in 64 bits, so inputs of several GiB do not wrap
// the padding offsets. A buffer of 4 GiB or more is impractical in CI, so sha1_chunk_count_is_64bit_safe checks
// the arithmetic at sizes where 32-bit math would wrap.

UTEST(hash, sha1_chunk_count_is_64bit_safe) {
    // Sizes a CI machine cannot allocate, passed to the function sha1digest() sizes its loop and tail with.
    u32 tail_bytes = 0;

    // 5 GiB, 64-byte aligned: the tail is one whole padding chunk.
    const u64 five_gib = 5ull * 1024 * 1024 * 1024;
    EXPECT_EQ(hash_sha1_chunk_count(five_gib, &tail_bytes), 83886081ull);
    EXPECT_EQ(tail_bytes, 64u);

    // Across the alignment residues around the 4 GiB boundary the tail stays in the valid padding range [9, 72].
    const u64 four_gib = 4ull * 1024 * 1024 * 1024;
    for (u64 offset = 0; offset < 64; offset++) {
        const u64 chunks = hash_sha1_chunk_count(four_gib + offset, &tail_bytes);
        EXPECT_EQ(chunks * 64 - tail_bytes, four_gib + offset);
        EXPECT_GE(tail_bytes, 9u);
        EXPECT_LE(tail_bytes, 72u);
    }

    // At 1 TiB the true chunk count no longer fits 32 bits.
    const u64 one_tib = 1ull << 40;
    const u64 huge_chunks = hash_sha1_chunk_count(one_tib, &tail_bytes);
    EXPECT_EQ(huge_chunks, one_tib / 64 + 1);
    EXPECT_GT(huge_chunks, 0xffffffffull);

    // Small sizes keep the standard layout: a 55-byte message pads into one chunk, 56 bytes needs two.
    const struct {
        u64 size;
        u64 chunks;
    } small_sizes[] = { { 55, 1ull }, { 56, 2ull } };
    for (size_t i = 0; i < sizeof_array(small_sizes); i++) {
        EXPECT_EQ(hash_sha1_chunk_count(small_sizes[i].size, &tail_bytes), small_sizes[i].chunks);
    }
}

// fl_hash_crc32 takes a u64 size but zlib's crc32() takes a 32-bit length, so buffers of 4 GiB or more are hashed
// by chaining crc32() calls over chunks. 4 GiB cannot be allocated here, so the case checks what the chunk loop
// relies on: crc32 chained over consecutive pieces equals crc32 of the whole.

UTEST(hash, crc32_chunked_matches_reference) {
    // CRC-32/ISO-HDLC canonical check value: crc32("123456789") == 0xCBF43926.
    EXPECT_EQ(fl_hash_crc32((const u8*)"123456789", 9), 0xCBF43926u);

    u8 buf[1024];
    for (int i = 0; i < 1024; i++) {
        buf[i] = (u8)(i * 31 + 7);
    }
    u32 whole = fl_hash_crc32(buf, sizeof(buf));

    for (u64 split = 0; split <= sizeof(buf); split += 37) {
        u32 crc = fl_hash_crc32(buf, split);
        crc = hash_crc32_update(crc, buf + split, sizeof(buf) - split);
        EXPECT_EQ(crc, whole);
    }

    // Empty input gives zlib's initial CRC (0), and an update over zero bytes leaves the running CRC unchanged.
    EXPECT_EQ(fl_hash_crc32(nullptr, 0), 0u);
    EXPECT_EQ(hash_crc32_update(whole, nullptr, 0), whole);
}
