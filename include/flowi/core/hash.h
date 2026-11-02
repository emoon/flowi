#pragma once

// One-shot message digests. The hex forms write the digest as lowercase hex followed by a null terminator.

#include <flowi/fl_api.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// CRC32 (zlib polynomial) of data.
FL_API uint32_t fl_hash_crc32(const uint8_t* data, uint64_t size);

// SHA1 digest (20 bytes) of data.
FL_API void fl_hash_sha1(const uint8_t* data, uint64_t size, uint8_t out[20]);

// MD5 digest (16 bytes) of data.
FL_API void fl_hash_md5(const uint8_t* data, uint64_t size, uint8_t out[16]);

// SHA256 of data as 64 hex characters plus a null terminator.
FL_API void fl_hash_sha256_hex(const uint8_t* data, uint64_t size, char out[65]);

// A 32-byte SHA256 digest as 64 hex characters plus a null terminator.
FL_API void fl_hash_sha256_digest_hex(const uint8_t digest[32], char out[65]);

#ifdef __cplusplus
}
#endif
