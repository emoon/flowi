#include "utest/utest.h"
#include <core/core.h>

#include <core/hash.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, crc32_empty) {
    u32 crc = fl_hash_crc32(nullptr, 0);
    ASSERT_EQ(crc, 0U);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, crc32_simple) {
    // "123456789" has well-known CRC32 = 0xCBF43926
    const char* data = "123456789";
    u32 crc = fl_hash_crc32((const u8*)data, 9);
    ASSERT_EQ(crc, 0xCBF43926U);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, crc32_incremental) {
    const char* data = "123456789";

    u32 crc = 0;
    crc = hash_crc32_update(crc, (const u8*)data, 5);
    crc = hash_crc32_update(crc, (const u8*)(data + 5), 4);

    ASSERT_EQ(crc, 0xCBF43926U);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, sha1_empty) {
    // A zero-length hash takes an empty buffer, not nullptr.
    u8 sha1[20];
    u8 empty_data = 0;
    fl_hash_sha1(&empty_data, 0, sha1);

    // SHA1("") = da39a3ee5e6b4b0d3255bfef95601890afd80709
    u8 expected[20] = { 0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
                        0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09 };
    ASSERT_EQ(memcmp(sha1, expected, 20), 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, sha1_abc) {
    u8 sha1[20];
    fl_hash_sha1((const u8*)"abc", 3, sha1);

    // SHA1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
    u8 expected[20] = { 0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                        0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d };
    ASSERT_EQ(memcmp(sha1, expected, 20), 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, sha1_hex) {
    char hex[41];
    hash_sha1_hex((const u8*)"abc", 3, hex);
    ASSERT_STREQ(hex, "a9993e364706816aba3e25717850c26c9cd0d89d");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, sha1_longer_message) {
    const char* msg = "The quick brown fox jumps over the lazy dog";
    char hex[41];
    hash_sha1_hex((const u8*)msg, strlen(msg), hex);
    ASSERT_STREQ(hex, "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, md5_empty) {
    u8 md5[16];
    u8 empty_data = 0;
    fl_hash_md5(&empty_data, 0, md5);

    // MD5("") = d41d8cd98f00b204e9800998ecf8427e
    u8 expected[16]
        = { 0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04, 0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e };
    ASSERT_EQ(memcmp(md5, expected, 16), 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, md5_abc) {
    u8 md5[16];
    fl_hash_md5((const u8*)"abc", 3, md5);

    // MD5("abc") = 900150983cd24fb0d6963f7d28e17f72
    u8 expected[16]
        = { 0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0, 0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72 };
    ASSERT_EQ(memcmp(md5, expected, 16), 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, md5_hex) {
    char hex[33];
    hash_md5_hex((const u8*)"abc", 3, hex);
    ASSERT_STREQ(hex, "900150983cd24fb0d6963f7d28e17f72");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, md5_longer_message) {
    const char* msg = "The quick brown fox jumps over the lazy dog";
    char hex[33];
    hash_md5_hex((const u8*)msg, strlen(msg), hex);
    ASSERT_STREQ(hex, "9e107d9d372bb6826bd81d3542a419d6");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, md5_incremental) {
    const char* msg = "The quick brown fox jumps over the lazy dog";
    size_t len = strlen(msg);

    Md5Context ctx;
    hash_md5_init(&ctx);
    hash_md5_update(&ctx, (const u8*)msg, 10);
    hash_md5_update(&ctx, (const u8*)(msg + 10), 20);
    hash_md5_update(&ctx, (const u8*)(msg + 30), len - 30);

    u8 md5[16];
    hash_md5_final(&ctx, md5);

    u8 expected[16];
    fl_hash_md5((const u8*)msg, len, expected);

    ASSERT_EQ(memcmp(md5, expected, 16), 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, sha256_empty) {
    u8 sha256[32];
    u8 empty_data = 0;
    hash_sha256(&empty_data, 0, sha256);

    // SHA256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    u8 expected[32]
        = { 0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
            0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55 };
    ASSERT_EQ(memcmp(sha256, expected, 32), 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, sha256_abc) {
    u8 sha256[32];
    hash_sha256((const u8*)"abc", 3, sha256);

    // SHA256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    u8 expected[32]
        = { 0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
            0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad };
    ASSERT_EQ(memcmp(sha256, expected, 32), 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, sha256_hex) {
    char hex[65];
    fl_hash_sha256_hex((const u8*)"abc", 3, hex);
    ASSERT_STREQ(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, sha256_longer_message) {
    const char* msg = "The quick brown fox jumps over the lazy dog";
    char hex[65];
    fl_hash_sha256_hex((const u8*)msg, strlen(msg), hex);
    ASSERT_STREQ(hex, "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hash, sha256_incremental) {
    const char* msg = "The quick brown fox jumps over the lazy dog";
    u64 len = strlen(msg);

    Sha256Context ctx;
    fl_hash_sha256_init(&ctx);
    fl_hash_sha256_update(&ctx, (const u8*)msg, 10);
    fl_hash_sha256_update(&ctx, (const u8*)(msg + 10), 20);
    fl_hash_sha256_update(&ctx, (const u8*)(msg + 30), len - 30);

    u8 sha256[32];
    fl_hash_sha256_final(&ctx, sha256);

    u8 expected[32];
    hash_sha256((const u8*)msg, len, expected);

    ASSERT_EQ(memcmp(sha256, expected, 32), 0);
}
