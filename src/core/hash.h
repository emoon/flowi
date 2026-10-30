// File hashing and message digests: CRC32 (zlib), SHA1, MD5 and SHA256.

#pragma once

#include "types.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CRC32 hashing (using zlib)

u32 fl_hash_crc32(const u8* data, u64 size);

// Compute CRC32 incrementally (start with crc=0)
u32 hash_crc32_update(u32 crc, const u8* data, u64 size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SHA1 hashing (using teeny-sha1)

void fl_hash_sha1(const u8* data, u64 size, u8 out[20]);

// out is 40 hex chars + null
void hash_sha1_hex(const u8* data, u64 size, char out[41]);

// Number of 64-byte chunks SHA1 hashes for a size-byte message, and the byte count of the final padded tail (the
// message's last partial chunk plus padding, in [9, 72]).
u64 hash_sha1_chunk_count(u64 size, u32* out_tail_bytes);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MD5 hashing (public domain implementation from Colin Plumb)

typedef struct Md5Context {
    u32 buf[4];
    u32 bits[2];
    u8 in[64];
} Md5Context;

void hash_md5_init(Md5Context* ctx);
void hash_md5_update(Md5Context* ctx, const u8* data, u64 size);
void hash_md5_final(Md5Context* ctx, u8 out[16]);
void fl_hash_md5(const u8* data, u64 size, u8 out[16]);

// out is 32 hex chars + null
void hash_md5_hex(const u8* data, u64 size, char out[33]);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SHA256 hashing (public domain implementation based on Brad Conte's)

typedef struct Sha256Context {
    u32 state[8];
    u64 bitcount;
    u8 data[64];
    u32 datalen;
} Sha256Context;

void fl_hash_sha256_init(Sha256Context* ctx);
void fl_hash_sha256_update(Sha256Context* ctx, const u8* data, u64 size);
void fl_hash_sha256_final(Sha256Context* ctx, u8 out[32]);
void hash_sha256(const u8* data, u64 size, u8 out[32]);

// out is 64 hex chars + null
void fl_hash_sha256_hex(const u8* data, u64 size, char out[65]);
void fl_hash_sha256_digest_hex(const u8 digest[32], char out[65]);
