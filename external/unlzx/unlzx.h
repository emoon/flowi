/*
 * unlzx.h - Amiga LZX archive decompression library
 *
 * Based on unlzx.c 1.1 by Erik Meusel and Dan Fraser (2001)
 * Original LZX algorithm by Jonathan Forbes and Tomi Poutanen (1995)
 *
 * Refactored for library use with context-based state and memory I/O.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef UNLZX_H
#define UNLZX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque context handle */
typedef struct UnlzxArchive UnlzxArchive;

/* Entry information */
typedef struct UnlzxEntry {
    const char* filename; /* Entry filename (owned by archive) */
    uint32_t unpack_size; /* Uncompressed size */
    uint32_t pack_size;   /* Compressed size (0 if merged) */
    uint32_t crc;         /* CRC32 of uncompressed data */
    uint8_t pack_mode;    /* Compression mode (0=store, 2=normal) */
    uint8_t attributes;   /* Amiga file attributes */
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} UnlzxEntry;

/* Error codes */
typedef enum UnlzxError {
    UNLZX_OK = 0,
    UNLZX_ERR_INVALID_ARCHIVE,  /* Not a valid LZX archive */
    UNLZX_ERR_CORRUPT_HEADER,   /* Header CRC mismatch */
    UNLZX_ERR_CORRUPT_DATA,     /* Data CRC mismatch or decompression error */
    UNLZX_ERR_OUT_OF_MEMORY,    /* Memory allocation failed */
    UNLZX_ERR_BUFFER_TOO_SMALL, /* Output buffer too small */
    UNLZX_ERR_INVALID_INDEX,    /* Entry index out of range */
    UNLZX_ERR_UNSUPPORTED,      /* Unsupported compression method */
    UNLZX_ERR_EOF,              /* Unexpected end of data */
} UnlzxError;

/*
 * Where the archive, and every stream opened on it, get their memory.
 * alloc returns NULL on failure; free is never passed NULL.
 */
typedef struct UnlzxAllocator {
    void* (*alloc)(void* user, size_t size);
    void (*free)(void* user, void* ptr);
    void* user;
} UnlzxAllocator;

/*
 * Open an LZX archive from memory buffer.
 * The data pointer must remain valid for the lifetime of the archive.
 * Returns NULL on error, check unlzx_get_last_error() for details.
 */
UnlzxArchive* unlzx_open_memory(const uint8_t* data, size_t size);

/*
 * As unlzx_open_memory, allocating through allocator (malloc and free when NULL).
 */
UnlzxArchive* unlzx_open_memory_with_allocator(const uint8_t* data, size_t size, const UnlzxAllocator* allocator);

/*
 * Get the number of entries in the archive.
 */
int unlzx_get_entry_count(UnlzxArchive* archive);

/*
 * Get entry information by index (0-based).
 * Returns NULL if index is out of range.
 * The returned pointer is valid until the archive is closed.
 */
const UnlzxEntry* unlzx_get_entry(UnlzxArchive* archive, int index);

/*
 * Find entry by filename.
 * Returns NULL if not found.
 */
const UnlzxEntry* unlzx_find_entry(UnlzxArchive* archive, const char* filename);

/*
 * Extract an entry to a buffer.
 * buffer must be at least entry->unpack_size bytes.
 * Returns UNLZX_OK on success, error code otherwise.
 */
UnlzxError unlzx_extract_entry(UnlzxArchive* archive, const UnlzxEntry* entry, uint8_t* buffer, size_t buffer_size);

/*
 * Streaming extraction: decompresses an entry in pieces of the caller's size,
 * keeping only fixed-size decoder state however large the entry is. Each
 * stream decodes independently of the archive and of other streams, but reads
 * the archive's data, so it must be closed before the archive.
 *
 * unlzx_stream_open sets *stream on success.
 * unlzx_stream_read decompresses up to size bytes into buffer and sets
 * *bytes_read, which is 0 once the whole entry has been read. The CRC is
 * checked by the read that produces the entry's last byte; a mismatch fails
 * that read. After an error every later read returns the same error.
 */
typedef struct UnlzxStream UnlzxStream;

UnlzxError unlzx_stream_open(UnlzxArchive* archive, const UnlzxEntry* entry, UnlzxStream** stream);
UnlzxError unlzx_stream_read(UnlzxStream* stream, uint8_t* buffer, size_t size, size_t* bytes_read);
void unlzx_stream_close(UnlzxStream* stream);

/*
 * Get the last error code.
 */
UnlzxError unlzx_get_last_error(UnlzxArchive* archive);

/*
 * Close the archive and free resources.
 */
void unlzx_close(UnlzxArchive* archive);

#ifdef __cplusplus
}
#endif

#endif /* UNLZX_H */
