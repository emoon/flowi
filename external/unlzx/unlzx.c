/*
 * unlzx.c - Amiga LZX archive decompression library
 *
 * Based on unlzx.c 1.1 by Erik Meusel and Dan Fraser (2001)
 * Original LZX algorithm by Jonathan Forbes and Tomi Poutanen (1995)
 *
 * Refactored for library use with context-based state and memory I/O.
 * This is free and unencumbered software released into the public domain.
 *
 * Original comments preserved:
 * "Everything is accessed as unsigned char's to try and avoid problems
 *  with byte order and alignment. Most of the decrunch functions
 *  encourage overruns in the buffers to make things as fast as possible.
 *  All the time is taken up in crc_calc() and decrunch() so they are
 *  pretty damn optimized. Don't try to understand this program."
 */

#include "unlzx.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Constants */

#define LZX_MAGIC_0 76 /* 'L' */
#define LZX_MAGIC_1 90 /* 'Z' */
#define LZX_MAGIC_2 88 /* 'X' */

#define INFO_HEADER_SIZE 10
#define ARCHIVE_HEADER_SIZE 31

#define READ_BUFFER_SIZE 16384
#define DECRUNCH_BUFFER_SIZE (258 + 65536 + 258)

#define MAX_ENTRIES 65536

/* ------------------------------------------------------------------------ */
/* Internal entry structure (includes data offset) */

typedef struct UnlzxEntryInternal {
    UnlzxEntry info;
    char filename[256];
    size_t data_offset;        /* Offset to packed data in archive */
    uint32_t actual_pack_size; /* Actual packed size for this entry */
} UnlzxEntryInternal;

/* ------------------------------------------------------------------------ */
/* Archive context structure */

struct UnlzxArchive {
    /* Input data */
    const uint8_t* data;
    size_t size;
    size_t pos;

    /* Entry list */
    UnlzxEntryInternal* entries;
    int entry_count;
    int entry_capacity;

    /* Decompression state */
    uint8_t read_buffer[READ_BUFFER_SIZE];
    uint8_t decrunch_buffer[DECRUNCH_BUFFER_SIZE];

    uint8_t* source;
    uint8_t* destination;
    uint8_t* source_end;
    uint8_t* destination_end;

    uint32_t decrunch_method;
    uint32_t decrunch_length;
    uint32_t last_offset;
    uint32_t global_control;
    int global_shift;

    uint8_t offset_len[8];
    uint16_t offset_table[128];
    uint8_t huffman20_len[20];
    uint16_t huffman20_table[96];
    uint8_t literal_len[768];
    uint16_t literal_table[5120];

    uint32_t sum; /* CRC accumulator */

    UnlzxError last_error;

    UnlzxAllocator allocator;
};

/* ------------------------------------------------------------------------ */
/* Allocation */

static void* default_alloc(void* user, size_t size) {
    (void)user;
    return malloc(size);
}

static void default_free(void* user, void* ptr) {
    (void)user;
    free(ptr);
}

static void* alloc_zeroed(const UnlzxAllocator* allocator, size_t size) {
    void* ptr = allocator->alloc(allocator->user, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/* ------------------------------------------------------------------------ */
/* CRC table */

static const uint32_t crc_table[256]
    = { 0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3, 0x0EDB8832,
        0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
        0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7, 0x136C9856, 0x646BA8C0, 0xFD62F97A,
        0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
        0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3,
        0x45DF5C75, 0xDCD60DCF, 0xABD13D59, 0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
        0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB,
        0xB6662D3D, 0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01, 0x6B6B51F4,
        0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
        0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65, 0x4DB26158, 0x3AB551CE, 0xA3BC0074,
        0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
        0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525,
        0x206F85B3, 0xB966D409, 0xCE61E49F, 0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
        0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615,
        0x73DC1683, 0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7, 0xFED41B76,
        0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
        0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B, 0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6,
        0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
        0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7,
        0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D, 0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
        0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7,
        0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45, 0xA00AE278,
        0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
        0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9, 0xBDBDF21C, 0xCABAC28A, 0x53B39330,
        0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
        0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D };

/* ------------------------------------------------------------------------ */
/* Decompression tables */

static const uint8_t table_one[32]
    = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14 };

static const uint32_t table_two[32]
    = { 0,   1,   2,   3,   4,    6,    8,    12,   16,   24,   32,   48,    64,    96,    128,   192,
        256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192, 12288, 16384, 24576, 32768, 49152 };

static const uint32_t table_three[16] = { 0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095, 8191, 16383, 32767 };

static const uint8_t table_four[34] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

/* ------------------------------------------------------------------------ */
/* Helper: Calculate CRC */

static void crc_calc(UnlzxArchive* ctx, const uint8_t* memory, uint32_t length) {
    if (length) {
        uint32_t temp = ~ctx->sum;
        do {
            temp = crc_table[(*memory++ ^ temp) & 255] ^ (temp >> 8);
        } while (--length);
        ctx->sum = ~temp;
    }
}

/* ------------------------------------------------------------------------ */
/* Helper: Read bytes from memory buffer */

static int mem_read(UnlzxArchive* ctx, uint8_t* buffer, size_t count) {
    size_t available = ctx->size - ctx->pos;
    if (count > available) {
        count = available;
    }
    if (count > 0) {
        memcpy(buffer, ctx->data + ctx->pos, count);
        ctx->pos += count;
    }
    return (int)count;
}

/* ------------------------------------------------------------------------ */
/* Helper: Seek in memory buffer */

static int mem_seek(UnlzxArchive* ctx, size_t offset) {
    if (offset > ctx->size) {
        return -1;
    }
    ctx->pos = offset;
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Build a fast huffman decode table from the symbol bit lengths. */

static int make_decode_table(int number_symbols, int table_size, uint8_t* length, uint16_t* table) {
    uint8_t bit_num = 0;
    int symbol;
    uint32_t leaf;
    uint32_t table_mask, bit_mask, pos, fill, next_symbol, reverse;
    int abort = 0;

    pos = 0;
    bit_mask = table_mask = 1u << table_size;
    bit_mask >>= 1;
    bit_num++;

    while ((!abort) && (bit_num <= table_size)) {
        for (symbol = 0; symbol < number_symbols; symbol++) {
            if (length[symbol] == bit_num) {
                reverse = pos;
                leaf = 0;
                fill = table_size;
                do {
                    leaf = (leaf << 1) + (reverse & 1);
                    reverse >>= 1;
                } while (--fill);
                if ((pos += bit_mask) > table_mask) {
                    abort = 1;
                    break;
                }
                fill = bit_mask;
                next_symbol = 1u << bit_num;
                do {
                    table[leaf] = (uint16_t)symbol;
                    leaf += next_symbol;
                } while (--fill);
            }
        }
        bit_mask >>= 1;
        bit_num++;
    }

    if ((!abort) && (pos != table_mask)) {
        for (symbol = (int)pos; symbol < (int)table_mask; symbol++) {
            reverse = (uint32_t)symbol;
            leaf = 0;
            fill = table_size;
            do {
                leaf = (leaf << 1) + (reverse & 1);
                reverse >>= 1;
            } while (--fill);
            table[leaf] = 0;
        }
        next_symbol = table_mask >> 1;
        pos <<= 16;
        table_mask <<= 16;
        bit_mask = 32768;

        while ((!abort) && (bit_num <= 16)) {
            for (symbol = 0; symbol < number_symbols; symbol++) {
                if (length[symbol] == bit_num) {
                    reverse = pos >> 16;
                    leaf = 0;
                    fill = table_size;
                    do {
                        leaf = (leaf << 1) + (reverse & 1);
                        reverse >>= 1;
                    } while (--fill);
                    for (fill = 0; fill < (uint32_t)(bit_num - table_size); fill++) {
                        if (table[leaf] == 0) {
                            table[(next_symbol << 1)] = 0;
                            table[(next_symbol << 1) + 1] = 0;
                            table[leaf] = (uint16_t)next_symbol++;
                        }
                        leaf = table[leaf] << 1;
                        leaf += (pos >> (15 - fill)) & 1;
                    }
                    table[leaf] = (uint16_t)symbol;
                    if ((pos += bit_mask) > table_mask) {
                        abort = 1;
                        break;
                    }
                }
            }
            bit_mask >>= 1;
            bit_num++;
        }
    }
    if (pos != table_mask)
        abort = 1;

    return abort;
}

/* ------------------------------------------------------------------------ */
/* Read and build the decrunch tables. */

static int read_literal_table(UnlzxArchive* ctx) {
    uint32_t control;
    int shift;
    uint32_t temp;
    uint32_t symbol, pos, count, fix, max_symbol;
    int abort = 0;

    control = ctx->global_control;
    shift = ctx->global_shift;

    if (shift < 0) {
        shift += 16;
        control += *ctx->source++ << (8 + shift);
        control += *ctx->source++ << shift;
    }

    ctx->decrunch_method = control & 7;
    control >>= 3;
    if ((shift -= 3) < 0) {
        shift += 16;
        control += *ctx->source++ << (8 + shift);
        control += *ctx->source++ << shift;
    }

    if ((!abort) && (ctx->decrunch_method == 3)) {
        for (temp = 0; temp < 8; temp++) {
            ctx->offset_len[temp] = (uint8_t)(control & 7);
            control >>= 3;
            if ((shift -= 3) < 0) {
                shift += 16;
                control += *ctx->source++ << (8 + shift);
                control += *ctx->source++ << shift;
            }
        }
        abort = make_decode_table(8, 7, ctx->offset_len, ctx->offset_table);
    }

    if (!abort) {
        ctx->decrunch_length = (control & 255) << 16;
        control >>= 8;
        if ((shift -= 8) < 0) {
            shift += 16;
            control += *ctx->source++ << (8 + shift);
            control += *ctx->source++ << shift;
        }
        ctx->decrunch_length += (control & 255) << 8;
        control >>= 8;
        if ((shift -= 8) < 0) {
            shift += 16;
            control += *ctx->source++ << (8 + shift);
            control += *ctx->source++ << shift;
        }
        ctx->decrunch_length += (control & 255);
        control >>= 8;
        if ((shift -= 8) < 0) {
            shift += 16;
            control += *ctx->source++ << (8 + shift);
            control += *ctx->source++ << shift;
        }
    }

    if ((!abort) && (ctx->decrunch_method != 1)) {
        pos = 0;
        fix = 1;
        max_symbol = 256;

        do {
            for (temp = 0; temp < 20; temp++) {
                ctx->huffman20_len[temp] = (uint8_t)(control & 15);
                control >>= 4;
                if ((shift -= 4) < 0) {
                    shift += 16;
                    control += *ctx->source++ << (8 + shift);
                    control += *ctx->source++ << shift;
                }
            }
            abort = make_decode_table(20, 6, ctx->huffman20_len, ctx->huffman20_table);

            if (abort)
                break;

            do {
                if ((symbol = ctx->huffman20_table[control & 63]) >= 20) {
                    do {
                        symbol = ctx->huffman20_table[((control >> 6) & 1) + (symbol << 1)];
                        if (!shift--) {
                            shift += 16;
                            control += *ctx->source++ << 24;
                            control += *ctx->source++ << 16;
                        }
                        control >>= 1;
                    } while (symbol >= 20);
                    temp = 6;
                } else {
                    temp = ctx->huffman20_len[symbol];
                }
                control >>= temp;
                if ((shift -= (int)temp) < 0) {
                    shift += 16;
                    control += *ctx->source++ << (8 + shift);
                    control += *ctx->source++ << shift;
                }
                switch (symbol) {
                    case 17:
                    case 18: {
                        if (symbol == 17) {
                            temp = 4;
                            count = 3;
                        } else {
                            temp = 6 - fix;
                            count = 19;
                        }
                        count += (control & table_three[temp]) + fix;
                        control >>= temp;
                        if ((shift -= (int)temp) < 0) {
                            shift += 16;
                            control += *ctx->source++ << (8 + shift);
                            control += *ctx->source++ << shift;
                        }
                        while ((pos < max_symbol) && (count--))
                            ctx->literal_len[pos++] = 0;
                        break;
                    }
                    case 19: {
                        count = (control & 1) + 3 + fix;
                        if (!shift--) {
                            shift += 16;
                            control += *ctx->source++ << 24;
                            control += *ctx->source++ << 16;
                        }
                        control >>= 1;
                        if ((symbol = ctx->huffman20_table[control & 63]) >= 20) {
                            do {
                                symbol = ctx->huffman20_table[((control >> 6) & 1) + (symbol << 1)];
                                if (!shift--) {
                                    shift += 16;
                                    control += *ctx->source++ << 24;
                                    control += *ctx->source++ << 16;
                                }
                                control >>= 1;
                            } while (symbol >= 20);
                            temp = 6;
                        } else {
                            temp = ctx->huffman20_len[symbol];
                        }
                        control >>= temp;
                        if ((shift -= (int)temp) < 0) {
                            shift += 16;
                            control += *ctx->source++ << (8 + shift);
                            control += *ctx->source++ << shift;
                        }
                        symbol = table_four[ctx->literal_len[pos] + 17 - symbol];
                        while ((pos < max_symbol) && (count--))
                            ctx->literal_len[pos++] = (uint8_t)symbol;
                        break;
                    }
                    default: {
                        symbol = table_four[ctx->literal_len[pos] + 17 - symbol];
                        ctx->literal_len[pos++] = (uint8_t)symbol;
                        break;
                    }
                }
            } while (pos < max_symbol);
            fix--;
            max_symbol += 512;
        } while (max_symbol == 768);

        if (!abort)
            abort = make_decode_table(768, 12, ctx->literal_len, ctx->literal_table);
    }

    ctx->global_control = control;
    ctx->global_shift = shift;

    return abort;
}

/* ------------------------------------------------------------------------ */
/* Fill up the decrunch buffer. */

static void decrunch(UnlzxArchive* ctx) {
    uint32_t control;
    int shift;
    uint32_t temp;
    uint32_t symbol, count;
    uint8_t* string;

    control = ctx->global_control;
    shift = ctx->global_shift;

    do {
        if ((symbol = ctx->literal_table[control & 4095]) >= 768) {
            control >>= 12;
            if ((shift -= 12) < 0) {
                shift += 16;
                control += *ctx->source++ << (8 + shift);
                control += *ctx->source++ << shift;
            }
            do {
                symbol = ctx->literal_table[(control & 1) + (symbol << 1)];
                if (!shift--) {
                    shift += 16;
                    control += *ctx->source++ << 24;
                    control += *ctx->source++ << 16;
                }
                control >>= 1;
            } while (symbol >= 768);
        } else {
            temp = ctx->literal_len[symbol];
            control >>= temp;
            if ((shift -= (int)temp) < 0) {
                shift += 16;
                control += *ctx->source++ << (8 + shift);
                control += *ctx->source++ << shift;
            }
        }
        if (symbol < 256) {
            *ctx->destination++ = (uint8_t)symbol;
        } else {
            symbol -= 256;
            count = table_two[temp = symbol & 31];
            temp = table_one[temp];
            if ((temp >= 3) && (ctx->decrunch_method == 3)) {
                temp -= 3;
                count += ((control & table_three[temp]) << 3);
                control >>= temp;
                if ((shift -= (int)temp) < 0) {
                    shift += 16;
                    control += *ctx->source++ << (8 + shift);
                    control += *ctx->source++ << shift;
                }
                count += (temp = ctx->offset_table[control & 127]);
                temp = ctx->offset_len[temp];
            } else {
                count += control & table_three[temp];
                if (!count)
                    count = ctx->last_offset;
            }
            control >>= temp;
            if ((shift -= (int)temp) < 0) {
                shift += 16;
                control += *ctx->source++ << (8 + shift);
                control += *ctx->source++ << shift;
            }
            ctx->last_offset = count;

            count = table_two[temp = (symbol >> 5) & 15] + 3;
            temp = table_one[temp];
            count += (control & table_three[temp]);
            control >>= temp;
            if ((shift -= (int)temp) < 0) {
                shift += 16;
                control += *ctx->source++ << (8 + shift);
                control += *ctx->source++ << shift;
            }
            string = (ctx->decrunch_buffer + ctx->last_offset < ctx->destination)
                         ? ctx->destination - ctx->last_offset
                         : ctx->destination + 65536 - ctx->last_offset;
            do {
                *ctx->destination++ = *string++;
            } while (--count);
        }
    } while ((ctx->destination < ctx->destination_end) && (ctx->source < ctx->source_end));

    ctx->global_control = control;
    ctx->global_shift = shift;
}

/* ------------------------------------------------------------------------ */
/* Resumable extraction of one entry. Only fixed-size decoder state is kept
 * between reads, so an entry can be read in pieces of any size. */

typedef struct UnlzxStreamState {
    uint32_t unpack_left;
    uint32_t pack_left;
    uint32_t crc;
    uint8_t pack_mode;
    uint8_t* pos;     /* Next decrunched byte not yet handed out */
    UnlzxError error; /* Sticky: a stream that failed stays failed */
} UnlzxStreamState;

struct UnlzxStream {
    UnlzxArchive ctx; /* Private decoder state over the archive's data */
    UnlzxStreamState state;
};

static const UnlzxEntryInternal* find_internal_entry(UnlzxArchive* archive, const UnlzxEntry* entry) {
    for (int i = 0; i < archive->entry_count; i++) {
        if (&archive->entries[i].info == entry) {
            return &archive->entries[i];
        }
    }
    return NULL;
}

static UnlzxError stream_begin(UnlzxArchive* ctx, UnlzxStreamState* s, const UnlzxEntryInternal* entry) {
    uint32_t count;

    memset(s, 0, sizeof(*s));
    s->unpack_left = entry->info.unpack_size;
    s->pack_left = entry->actual_pack_size;
    s->crc = entry->info.crc;
    s->pack_mode = entry->info.pack_mode;

    if (s->pack_mode != 0 && s->pack_mode != 2) {
        return UNLZX_ERR_UNSUPPORTED;
    }

    if (mem_seek(ctx, entry->data_offset) != 0) {
        return UNLZX_ERR_EOF;
    }

    ctx->sum = 0;

    if (s->pack_mode == 2) {
        ctx->global_control = 0;
        ctx->global_shift = -16;
        ctx->last_offset = 1;
        ctx->decrunch_length = 0;

        for (count = 0; count < 8; count++)
            ctx->offset_len[count] = 0;
        for (count = 0; count < 768; count++)
            ctx->literal_len[count] = 0;

        ctx->source_end = (ctx->source = ctx->read_buffer + READ_BUFFER_SIZE) - 1024;
        s->pos = ctx->destination_end = ctx->destination = ctx->decrunch_buffer + 258 + 65536;
    }

    return UNLZX_OK;
}

/* Decrunch the next run of bytes once everything decrunched so far is handed out. */
static UnlzxError stream_decrunch(UnlzxArchive* ctx, UnlzxStreamState* s) {
    uint8_t* temp;
    uint32_t count;

    if (ctx->source >= ctx->source_end) {
        temp = ctx->read_buffer;
        count = (uint32_t)(temp - ctx->source + READ_BUFFER_SIZE);
        if (count > 0) {
            do {
                *temp++ = *ctx->source++;
            } while (--count);
        }
        ctx->source = ctx->read_buffer;
        count = (uint32_t)(ctx->source - temp + READ_BUFFER_SIZE);

        if (s->pack_left < count)
            count = s->pack_left;

        if ((uint32_t)mem_read(ctx, temp, count) != count) {
            return UNLZX_ERR_EOF;
        }
        s->pack_left -= count;

        temp += count;
        if (ctx->source >= temp) {
            return UNLZX_ERR_EOF;
        }
    }

    if (ctx->decrunch_length <= 0) {
        if (read_literal_table(ctx)) {
            return UNLZX_ERR_CORRUPT_DATA;
        }
    }

    if (ctx->destination >= ctx->decrunch_buffer + 258 + 65536) {
        count = (uint32_t)(ctx->destination - ctx->decrunch_buffer - 65536);
        if (count > 0) {
            temp = (ctx->destination = ctx->decrunch_buffer) + 65536;
            do {
                *ctx->destination++ = *temp++;
            } while (--count);
        }
        s->pos = ctx->destination;
    }
    ctx->destination_end = ctx->destination + ctx->decrunch_length;
    if (ctx->destination_end > ctx->decrunch_buffer + 258 + 65536)
        ctx->destination_end = ctx->decrunch_buffer + 258 + 65536;
    temp = ctx->destination;

    decrunch(ctx);

    ctx->decrunch_length -= (uint32_t)(ctx->destination - temp);
    return UNLZX_OK;
}

/* Hand out up to size bytes of the entry. The CRC is checked by the read that
 * produces the entry's last byte. */
static UnlzxError stream_read(UnlzxArchive* ctx, UnlzxStreamState* s, uint8_t* output, size_t size,
                              size_t* bytes_read) {
    size_t done = 0;
    uint32_t count;

    *bytes_read = 0;
    if (s->error != UNLZX_OK) {
        return s->error;
    }

    while (done < size && s->unpack_left > 0) {
        count = s->unpack_left;
        if (count > size - done)
            count = (uint32_t)(size - done);

        if (s->pack_mode == 2) {
            if (s->pos == ctx->destination) {
                s->error = stream_decrunch(ctx, s);
                if (s->error != UNLZX_OK) {
                    return s->error;
                }
            }
            if (count > (uint32_t)(ctx->destination - s->pos))
                count = (uint32_t)(ctx->destination - s->pos);
            memcpy(output + done, s->pos, count);
            s->pos += count;
        } else {
            if (count > s->pack_left || (uint32_t)mem_read(ctx, output + done, count) != count) {
                s->error = UNLZX_ERR_EOF;
                return s->error;
            }
            s->pack_left -= count;
        }

        crc_calc(ctx, output + done, count);
        done += count;
        s->unpack_left -= count;
    }

    if (s->unpack_left == 0 && ctx->sum != s->crc) {
        s->error = UNLZX_ERR_CORRUPT_DATA;
        return s->error;
    }

    *bytes_read = done;
    return UNLZX_OK;
}

/* ------------------------------------------------------------------------ */
/* Add entry to archive */

static int add_entry(UnlzxArchive* ctx, const UnlzxEntryInternal* entry) {
    if (ctx->entry_count >= ctx->entry_capacity) {
        int new_capacity = ctx->entry_capacity ? ctx->entry_capacity * 2 : 64;
        if (new_capacity > MAX_ENTRIES)
            new_capacity = MAX_ENTRIES;
        if (ctx->entry_count >= new_capacity) {
            return -1;
        }
        UnlzxEntryInternal* new_entries
            = ctx->allocator.alloc(ctx->allocator.user, new_capacity * sizeof(UnlzxEntryInternal));
        if (!new_entries) {
            return -1;
        }
        if (ctx->entries) {
            memcpy(new_entries, ctx->entries, ctx->entry_count * sizeof(UnlzxEntryInternal));
            ctx->allocator.free(ctx->allocator.user, ctx->entries);
        }
        ctx->entries = new_entries;
        ctx->entry_capacity = new_capacity;
    }

    ctx->entries[ctx->entry_count++] = *entry;
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Parse archive headers and build entry list */

static UnlzxError parse_archive(UnlzxArchive* ctx) {
    uint8_t info_header[INFO_HEADER_SIZE];
    uint8_t archive_header[ARCHIVE_HEADER_SIZE];
    uint8_t header_filename[256];
    uint8_t header_comment[256];
    int actual;
    uint32_t crc;
    uint32_t temp;
    uint32_t pack_size;
    size_t data_offset;

    /* Read and verify info header */
    actual = mem_read(ctx, info_header, INFO_HEADER_SIZE);
    if (actual != INFO_HEADER_SIZE) {
        return UNLZX_ERR_EOF;
    }

    if (info_header[0] != LZX_MAGIC_0 || info_header[1] != LZX_MAGIC_1 || info_header[2] != LZX_MAGIC_2) {
        return UNLZX_ERR_INVALID_ARCHIVE;
    }

    /* Parse entry headers */
    while (1) {
        actual = mem_read(ctx, archive_header, ARCHIVE_HEADER_SIZE);
        if (actual == 0) {
            /* Normal EOF */
            break;
        }
        if (actual != ARCHIVE_HEADER_SIZE) {
            return UNLZX_ERR_EOF;
        }

        /* Verify header CRC */
        ctx->sum = 0;
        crc = ((uint32_t)archive_header[29] << 24) + ((uint32_t)archive_header[28] << 16)
              + ((uint32_t)archive_header[27] << 8) + archive_header[26];
        archive_header[29] = 0;
        archive_header[28] = 0;
        archive_header[27] = 0;
        archive_header[26] = 0;
        crc_calc(ctx, archive_header, ARCHIVE_HEADER_SIZE);

        /* Read filename */
        temp = archive_header[30];
        actual = mem_read(ctx, header_filename, temp);
        if ((uint32_t)actual != temp) {
            return UNLZX_ERR_EOF;
        }
        header_filename[temp] = 0;
        crc_calc(ctx, header_filename, temp);

        /* Read comment */
        temp = archive_header[14];
        actual = mem_read(ctx, header_comment, temp);
        if ((uint32_t)actual != temp) {
            return UNLZX_ERR_EOF;
        }
        header_comment[temp] = 0;
        crc_calc(ctx, header_comment, temp);

        if (ctx->sum != crc) {
            return UNLZX_ERR_CORRUPT_HEADER;
        }

        /* Parse entry info */
        UnlzxEntryInternal entry;
        memset(&entry, 0, sizeof(entry));

        entry.info.attributes = archive_header[0];
        entry.info.unpack_size = ((uint32_t)archive_header[5] << 24) + ((uint32_t)archive_header[4] << 16)
                                 + ((uint32_t)archive_header[3] << 8) + archive_header[2];
        pack_size = ((uint32_t)archive_header[9] << 24) + ((uint32_t)archive_header[8] << 16)
                    + ((uint32_t)archive_header[7] << 8) + archive_header[6];
        entry.info.pack_mode = archive_header[11];
        entry.info.crc = ((uint32_t)archive_header[25] << 24) + ((uint32_t)archive_header[24] << 16)
                         + ((uint32_t)archive_header[23] << 8) + archive_header[22];

        /* Parse date */
        temp = ((uint32_t)archive_header[18] << 24) + ((uint32_t)archive_header[19] << 16)
               + ((uint32_t)archive_header[20] << 8) + archive_header[21];
        entry.info.year = (uint16_t)(((temp >> 17) & 63) + 1970);
        entry.info.month = (uint8_t)((temp >> 23) & 15);
        entry.info.day = (uint8_t)((temp >> 27) & 31);
        entry.info.hour = (uint8_t)((temp >> 12) & 31);
        entry.info.minute = (uint8_t)((temp >> 6) & 63);
        entry.info.second = (uint8_t)(temp & 63);

        strncpy(entry.filename, (char*)header_filename, sizeof(entry.filename) - 1);
        entry.filename[sizeof(entry.filename) - 1] = 0;
        entry.info.filename = entry.filename;

        /* Record data position */
        data_offset = ctx->pos;
        entry.data_offset = data_offset;
        entry.actual_pack_size = pack_size;

        /* For merged entries, pack_size in header is 0 */
        entry.info.pack_size = pack_size;

        if (add_entry(ctx, &entry) < 0) {
            return UNLZX_ERR_OUT_OF_MEMORY;
        }

        /* Skip past packed data */
        if (pack_size > 0) {
            if (mem_seek(ctx, ctx->pos + pack_size) != 0) {
                return UNLZX_ERR_EOF;
            }
        }
    }

    /* Fix up filename pointers after the entry array moved */
    for (int i = 0; i < ctx->entry_count; i++) {
        ctx->entries[i].info.filename = ctx->entries[i].filename;
    }

    return UNLZX_OK;
}

/* ------------------------------------------------------------------------ */
/* Public API */

UnlzxArchive* unlzx_open_memory(const uint8_t* data, size_t size) {
    return unlzx_open_memory_with_allocator(data, size, NULL);
}

UnlzxArchive* unlzx_open_memory_with_allocator(const uint8_t* data, size_t size, const UnlzxAllocator* allocator) {
    static const UnlzxAllocator default_allocator = { default_alloc, default_free, NULL };
    if (!allocator) {
        allocator = &default_allocator;
    }

    if (!data || size < INFO_HEADER_SIZE) {
        return NULL;
    }

    UnlzxArchive* ctx = alloc_zeroed(allocator, sizeof(UnlzxArchive));
    if (!ctx) {
        return NULL;
    }

    ctx->allocator = *allocator;

    ctx->data = data;
    ctx->size = size;
    ctx->pos = 0;

    UnlzxError err = parse_archive(ctx);
    if (err != UNLZX_OK) {
        ctx->last_error = err;
        unlzx_close(ctx);
        return NULL;
    }

    return ctx;
}

int unlzx_get_entry_count(UnlzxArchive* archive) {
    return archive ? archive->entry_count : 0;
}

const UnlzxEntry* unlzx_get_entry(UnlzxArchive* archive, int index) {
    if (!archive || index < 0 || index >= archive->entry_count) {
        return NULL;
    }
    return &archive->entries[index].info;
}

const UnlzxEntry* unlzx_find_entry(UnlzxArchive* archive, const char* filename) {
    if (!archive || !filename) {
        return NULL;
    }

    for (int i = 0; i < archive->entry_count; i++) {
        if (strcmp(archive->entries[i].filename, filename) == 0) {
            return &archive->entries[i].info;
        }
    }

    return NULL;
}

UnlzxError unlzx_extract_entry(UnlzxArchive* archive, const UnlzxEntry* entry, uint8_t* buffer, size_t buffer_size) {
    if (!archive || !entry || !buffer) {
        return UNLZX_ERR_INVALID_INDEX;
    }

    if (buffer_size < entry->unpack_size) {
        return UNLZX_ERR_BUFFER_TOO_SMALL;
    }

    const UnlzxEntryInternal* internal = find_internal_entry(archive, entry);
    if (!internal) {
        return UNLZX_ERR_INVALID_INDEX;
    }

    UnlzxStreamState state;
    size_t bytes_read;
    UnlzxError err = stream_begin(archive, &state, internal);
    if (err == UNLZX_OK) {
        err = stream_read(archive, &state, buffer, entry->unpack_size, &bytes_read);
    }

    archive->last_error = err;
    return err;
}

UnlzxError unlzx_stream_open(UnlzxArchive* archive, const UnlzxEntry* entry, UnlzxStream** stream) {
    if (!archive || !entry || !stream) {
        return UNLZX_ERR_INVALID_INDEX;
    }
    *stream = NULL;

    const UnlzxEntryInternal* internal = find_internal_entry(archive, entry);
    if (!internal) {
        return UNLZX_ERR_INVALID_INDEX;
    }

    UnlzxStream* s = alloc_zeroed(&archive->allocator, sizeof(UnlzxStream));
    if (!s) {
        return UNLZX_ERR_OUT_OF_MEMORY;
    }
    s->ctx.data = archive->data;
    s->ctx.size = archive->size;
    s->ctx.allocator = archive->allocator;

    UnlzxError err = stream_begin(&s->ctx, &s->state, internal);
    if (err != UNLZX_OK) {
        unlzx_stream_close(s);
        return err;
    }

    *stream = s;
    return UNLZX_OK;
}

UnlzxError unlzx_stream_read(UnlzxStream* stream, uint8_t* buffer, size_t size, size_t* bytes_read) {
    if (!stream || !buffer || !bytes_read) {
        return UNLZX_ERR_INVALID_INDEX;
    }
    return stream_read(&stream->ctx, &stream->state, buffer, size, bytes_read);
}

void unlzx_stream_close(UnlzxStream* stream) {
    if (stream) {
        stream->ctx.allocator.free(stream->ctx.allocator.user, stream);
    }
}

UnlzxError unlzx_get_last_error(UnlzxArchive* archive) {
    return archive ? archive->last_error : UNLZX_ERR_INVALID_ARCHIVE;
}

void unlzx_close(UnlzxArchive* archive) {
    if (archive) {
        UnlzxAllocator allocator = archive->allocator;
        if (archive->entries) {
            allocator.free(allocator.user, archive->entries);
        }
        allocator.free(allocator.user, archive);
    }
}
