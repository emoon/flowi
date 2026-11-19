// File hashing and message digests: CRC32 (zlib), SHA1, MD5 and SHA256.

#include "hash.h"
#include "assert.h"
#include <zlib.h>

// CRC32

// zlib's crc32() takes a 32-bit uInt length; larger buffers are hashed in chained chunks of at most this size.
#define CRC32_MAX_CHUNK 0xffffffffu

u32 fl_hash_crc32(const u8* data, u64 size) {
    return hash_crc32_update(0, data, size);
}

u32 hash_crc32_update(u32 crc, const u8* data, u64 size) {
    uLong result = crc;
    while (size > 0) {
        uInt chunk = size > CRC32_MAX_CHUNK ? CRC32_MAX_CHUNK : (uInt)size;
        result = crc32(result, data, chunk);
        data += chunk;
        size -= chunk;
    }
    return (u32)result;
}

#undef CRC32_MAX_CHUNK

// SHA1 - teeny-sha1 implementation
//
// https://github.com/CTrabant/teeny-sha1
//
// Copyright (c) 2017 CTrabant
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SHA1ROTATELEFT(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

// Both values are 64-bit for multi-GiB inputs; the tail is always in [9, 72].
u64 hash_sha1_chunk_count(u64 size, u32* out_tail_bytes) {
    const u64 chunk_count = (size + 8) / 64 + 1;
    *out_tail_bytes = (u32)(64 * chunk_count - size);
    return chunk_count;
}

static void sha1_fill_tail(uint8_t* datatail, uint32_t tailbytes, uint64_t databits) {
    // Add bit '1' to end of message (big-endian)
    // Add 64-bit message length in bits at very end (big-endian)
    datatail[0] = 0x80;
    for (uint32_t i = 0; i < 8; i++) {
        datatail[tailbytes - 1 - i] = (uint8_t)(databits >> (i * 8) & 0xFF);
    }
}

typedef struct Sha1Message {
    const uint8_t* data;
    size_t databytes;
    const uint8_t* datatail;
} Sha1Message;

// Break one 512-bit chunk into sixteen 32-bit, big endian words, drawing from the message and then the padded tail
static void sha1_load_words(uint32_t* W, const Sha1Message* msg, uint64_t* didx) {
    memset(W, 0, 80 * sizeof(uint32_t));

    for (uint32_t widx = 0; widx <= 15; widx++) {
        int32_t wcount = 24;

        // Copy byte-per byte from specified buffer
        while (*didx < msg->databytes && wcount >= 0) {
            W[widx] += (((uint32_t)msg->data[*didx]) << wcount);
            (*didx)++;
            wcount -= 8;
        }
        // Fill out W with padding as needed
        while (wcount >= 0) {
            W[widx] += (((uint32_t)msg->datatail[*didx - msg->databytes]) << wcount);
            (*didx)++;
            wcount -= 8;
        }
    }
}

// Extend the sixteen 32-bit words into eighty 32-bit words
static void sha1_extend_words(uint32_t* W) {
    for (uint32_t widx = 16; widx <= 31; widx++) {
        W[widx] = SHA1ROTATELEFT((W[widx - 3] ^ W[widx - 8] ^ W[widx - 14] ^ W[widx - 16]), 1);
    }
    for (uint32_t widx = 32; widx <= 79; widx++) {
        W[widx] = SHA1ROTATELEFT((W[widx - 6] ^ W[widx - 16] ^ W[widx - 28] ^ W[widx - 32]), 2);
    }
}

static uint32_t sha1_round_function(uint32_t idx, uint32_t b, uint32_t c, uint32_t d) {
    if (idx <= 19) {
        return (b & c) | ((~b) & d);
    }
    if (idx <= 59 && idx >= 40) {
        return (b & c) | (b & d) | (c & d);
    }
    return b ^ c ^ d;
}

static uint32_t sha1_round_constant(uint32_t idx) {
    static const uint32_t constants[] = { 0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6 };
    return constants[idx / 20];
}

static void sha1_compress_chunk(uint32_t* H, const uint32_t* W) {
    uint32_t a = H[0];
    uint32_t b = H[1];
    uint32_t c = H[2];
    uint32_t d = H[3];
    uint32_t e = H[4];

    for (uint32_t idx = 0; idx <= 79; idx++) {
        uint32_t temp = SHA1ROTATELEFT(a, 5) + sha1_round_function(idx, b, c, d) + e + sha1_round_constant(idx) + W[idx];
        e = d;
        d = c;
        c = SHA1ROTATELEFT(b, 30);
        b = a;
        a = temp;
    }

    H[0] += a;
    H[1] += b;
    H[2] += c;
    H[3] += d;
    H[4] += e;
}

static void sha1_store_digest(uint8_t* digest, const uint32_t* H) {
    for (uint32_t idx = 0; idx < 5; idx++) {
        digest[idx * 4 + 0] = (uint8_t)(H[idx] >> 24);
        digest[idx * 4 + 1] = (uint8_t)(H[idx] >> 16);
        digest[idx * 4 + 2] = (uint8_t)(H[idx] >> 8);
        digest[idx * 4 + 3] = (uint8_t)(H[idx]);
    }
}

static int sha1digest(uint8_t* digest, char* hexdigest, const uint8_t* data, size_t databytes) {
    uint32_t W[80];
    uint32_t H[] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint64_t didx = 0;
    uint32_t tailbytes = 0;
    uint64_t loopcount = hash_sha1_chunk_count((uint64_t)databytes, &tailbytes);
    uint8_t datatail[128] = { 0 };

    if (!digest && !hexdigest) {
        return -1;
    }

    // nullptr data with zero length is the empty message.
    FL_VALIDATE_RET(data != nullptr || databytes == 0, -1);

    // Pre-processing of data tail (includes padding to fill out 512-bit chunk)
    sha1_fill_tail(datatail, tailbytes, ((uint64_t)databytes) * 8);

    Sha1Message msg = { data, databytes, datatail };

    for (uint64_t lidx = 0; lidx < loopcount; lidx++) {
        sha1_load_words(W, &msg, &didx);
        sha1_extend_words(W);
        sha1_compress_chunk(H, W);
    }

    if (digest) {
        sha1_store_digest(digest, H);
    }

    // Store hex version of digest in supplied buffer
    if (hexdigest) {
        snprintf(hexdigest, 41, "%08x%08x%08x%08x%08x", H[0], H[1], H[2], H[3], H[4]);
    }

    return 0;
}

#undef SHA1ROTATELEFT

void fl_hash_sha1(const u8* data, u64 size, u8 out[20]) {
    sha1digest(out, nullptr, data, (size_t)size);
}

void hash_sha1_hex(const u8* data, u64 size, char out[41]) {
    sha1digest(nullptr, out, data, (size_t)size);
}

// MD5 - public domain implementation
//
// This code implements the MD5 message-digest algorithm.
// The algorithm is due to Ron Rivest. This code was written by Colin Plumb
// in 1993, no copyright is claimed. This code is in the public domain.

// The four core functions
#define MD5_F1(x, y, z) (z ^ (x & (y ^ z)))
#define MD5_F2(x, y, z) MD5_F1(z, x, y)
#define MD5_F3(x, y, z) (x ^ y ^ z)
#define MD5_F4(x, y, z) (y ^ (x | ~z))

static const u32 md5_k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

static const u8 md5_shift[4][4] = { { 7, 12, 17, 22 }, { 5, 9, 14, 20 }, { 4, 11, 16, 23 }, { 6, 10, 15, 21 } };

static u32 md5_mix(u32 round, u32 x, u32 y, u32 z) {
    switch (round) {
        case 0: return MD5_F1(x, y, z);
        case 1: return MD5_F2(x, y, z);
        case 2: return MD5_F3(x, y, z);
        default: return MD5_F4(x, y, z);
    }
}

static u32 md5_word_index(u32 round, u32 step) {
    static const u32 mul[4] = { 1, 5, 3, 7 };
    static const u32 add[4] = { 0, 1, 5, 0 };
    return (mul[round] * step + add[round]) % 16;
}

static void md5_transform(u32 buf[4], const u32 in[16]) {
    u32 a = buf[0];
    u32 b = buf[1];
    u32 c = buf[2];
    u32 d = buf[3];

    for_count(step, 64) {
        u32 round = step / 16;
        u32 sum = a + md5_mix(round, b, c, d) + md5_k[step] + in[md5_word_index(round, step % 16)];
        u32 shift = md5_shift[round][step % 4];
        u32 next_b = b + (sum << shift | sum >> (32 - shift));
        a = d;
        d = c;
        c = b;
        b = next_b;
    }

    buf[0] += a;
    buf[1] += b;
    buf[2] += c;
    buf[3] += d;
}

#undef MD5_F1
#undef MD5_F2
#undef MD5_F3
#undef MD5_F4

void hash_md5_init(Md5Context* ctx) {
    ctx->buf[0] = 0x67452301;
    ctx->buf[1] = 0xefcdab89;
    ctx->buf[2] = 0x98badcfe;
    ctx->buf[3] = 0x10325476;
    ctx->bits[0] = 0;
    ctx->bits[1] = 0;
}

void hash_md5_update(Md5Context* ctx, const u8* data, u64 size) {
    u32 t = ctx->bits[0];
    if ((ctx->bits[0] = t + ((u32)size << 3)) < t) {
        ctx->bits[1]++; // Carry from low to high
    }
    ctx->bits[1] += (u32)(size >> 29);

    t = (t >> 3) & 0x3f; // Bytes already in ctx->in

    // Handle any leading odd-sized chunks
    if (t) {
        u8* p = ctx->in + t;
        t = 64 - t;
        if (size < t) {
            memcpy(p, data, (size_t)size);
            return;
        }
        memcpy(p, data, t);
        md5_transform(ctx->buf, (u32*)ctx->in);
        data += t;
        size -= t;
    }

    // Process data in 64-byte chunks
    while (size >= 64) {
        memcpy(ctx->in, data, 64);
        md5_transform(ctx->buf, (u32*)ctx->in);
        data += 64;
        size -= 64;
    }

    // Handle any remaining bytes
    memcpy(ctx->in, data, (size_t)size);
}

void hash_md5_final(Md5Context* ctx, u8 out[16]) {
    // Number of bytes mod 64
    u32 count = (ctx->bits[0] >> 3) & 0x3f;

    // Set first padding byte to 0x80
    u8* p = ctx->in + count;
    *p++ = 0x80;

    // Bytes of padding needed to make 64 bytes
    count = 64 - 1 - count;

    // Pad out to 56 mod 64
    if (count < 8) {
        // Two lots of padding
        memset(p, 0, count);
        md5_transform(ctx->buf, (u32*)ctx->in);
        memset(ctx->in, 0, 56);
    } else {
        memset(p, 0, count - 8);
    }

    // Append length in bits
    memcpy(&ctx->in[56], ctx->bits, 8);
    md5_transform(ctx->buf, (u32*)ctx->in);

    // Copy digest out
    memcpy(out, ctx->buf, 16);

    // Zero sensitive data
    memset(ctx, 0, sizeof(*ctx));
}

void fl_hash_md5(const u8* data, u64 size, u8 out[16]) {
    Md5Context ctx;
    hash_md5_init(&ctx);
    hash_md5_update(&ctx, data, size);
    hash_md5_final(&ctx, out);
}

void hash_md5_hex(const u8* data, u64 size, char out[33]) {
    u8 digest[16];
    fl_hash_md5(data, size, digest);
    for (int i = 0; i < 16; i++) {
        snprintf(out + i * 2, 3, "%02x", digest[i]);
    }
}

// SHA256 - public domain implementation
//
// Based on Brad Conte's compact SHA256. Public domain.

#define SHA256_ROTR(a, b) (((a) >> (b)) | ((a) << (32 - (b))))
#define SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x) (SHA256_ROTR(x, 2) ^ SHA256_ROTR(x, 13) ^ SHA256_ROTR(x, 22))
#define SHA256_EP1(x) (SHA256_ROTR(x, 6) ^ SHA256_ROTR(x, 11) ^ SHA256_ROTR(x, 25))
#define SHA256_SIG0(x) (SHA256_ROTR(x, 7) ^ SHA256_ROTR(x, 18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR(x, 17) ^ SHA256_ROTR(x, 19) ^ ((x) >> 10))

static const u32 sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static void sha256_transform(Sha256Context* ctx, const u8 block[64]) {
    u32 a, b, c, d, e, f, g, h, t1, t2, m[64];

    for (int i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = ((u32)block[j] << 24) | ((u32)block[j + 1] << 16) | ((u32)block[j + 2] << 8) | ((u32)block[j + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        m[i] = SHA256_SIG1(m[i - 2]) + m[i - 7] + SHA256_SIG0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (int i = 0; i < 64; ++i) {
        t1 = h + SHA256_EP1(e) + SHA256_CH(e, f, g) + sha256_k[i] + m[i];
        t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

#undef SHA256_ROTR
#undef SHA256_CH
#undef SHA256_MAJ
#undef SHA256_EP0
#undef SHA256_EP1
#undef SHA256_SIG0
#undef SHA256_SIG1

void fl_hash_sha256_init(Sha256Context* ctx) {
    ctx->datalen = 0;
    ctx->bitcount = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

void fl_hash_sha256_update(Sha256Context* ctx, const u8* data, u64 size) {
    for (u64 i = 0; i < size; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitcount += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_pad_block(Sha256Context* ctx) {
    u32 i = ctx->datalen;
    u32 pad_end = ctx->datalen < 56 ? 56 : 64;

    ctx->data[i++] = 0x80;
    memset(ctx->data + i, 0x00, pad_end - i);

    if (pad_end == 64) {
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }
}

void fl_hash_sha256_final(Sha256Context* ctx, u8 out[32]) {
    sha256_pad_block(ctx);

    // Append total message length in bits (big-endian)
    ctx->bitcount += (u64)ctx->datalen * 8;
    for (int k = 0; k < 8; ++k) {
        ctx->data[63 - k] = (u8)(ctx->bitcount >> (k * 8));
    }
    sha256_transform(ctx, ctx->data);

    // Output digest (big-endian)
    for (int j = 0; j < 8; ++j) {
        out[j * 4 + 0] = (u8)(ctx->state[j] >> 24);
        out[j * 4 + 1] = (u8)(ctx->state[j] >> 16);
        out[j * 4 + 2] = (u8)(ctx->state[j] >> 8);
        out[j * 4 + 3] = (u8)(ctx->state[j]);
    }
}

void hash_sha256(const u8* data, u64 size, u8 out[32]) {
    Sha256Context ctx;
    fl_hash_sha256_init(&ctx);
    fl_hash_sha256_update(&ctx, data, size);
    fl_hash_sha256_final(&ctx, out);
}

void fl_hash_sha256_hex(const u8* data, u64 size, char out[65]) {
    u8 digest[32];
    hash_sha256(data, size, digest);
    fl_hash_sha256_digest_hex(digest, out);
}

void fl_hash_sha256_digest_hex(const u8 digest[32], char out[65]) {
    for (int i = 0; i < 32; i++) {
        snprintf(out + i * 2, 3, "%02x", digest[i]);
    }
}
