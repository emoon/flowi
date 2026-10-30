#include "font_cache.h"
#include <core/arena.h>
#include <core/hashmap.h>
#include <core/log.h>
#include <core/os/os.h>
#include <core/utf8.h>
#include <vfs/vfs.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared font layer: FreeType face management, async file load, text measurement, per-size vertical metrics, glyph
// advances, and the raster-source hand-off. This layer NEVER rasterizes - coverage bitmaps, atlases, and the
// job-threaded realization live in the SW backend (render/software/sw_glyph_cache.{h,c}).

#define MAX_FONT_FACES 128

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FontFace {
    FlArena* arena;
    FlFontRasterKind kind;
    FlFontId id;
    _Atomic FontStatus status;
    FlVfsHandle vfs_handle;
    u16 size; // Nominal size (FreeType: load size; default: 8*scale)

    // FreeType source
    FT_Face metrics_face; // Main thread only - for text measurement
    u8* font_data;        // Font file data, kept so the SW backend can create raster faces
    i64 font_data_size;

    // Default source
    u16 default_scale;

    // Memoized glyph advances (FreeType faces). Sizes are quarter-pixel keys (round(px × 4)) so the whole-px
    // layout path and the fractional-size measurement path share one cache. ASCII at the active size is a dense
    // array; everything else (other sizes, codepoints >= 128) falls back to the hashmap, keyed
    // (quarter_px_key << 21) | codepoint. Advances are in 26.6 fixed point; -1 marks an uncomputed ASCII slot.
    int ascii_advance[128];
    u16 ascii_advance_size; // quarter-px key the ascii_advance row is valid for; 0 = none
    hashmap(u64, int) advance_cache;
} FontFace;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FontLoadContext {
    FlFontCache* cache;
    FontFace* face;
    u16 font_size;
} FontLoadContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlFontCache {
    FT_Library library;
    // Serializes all access to the FreeType library and its faces (no internal locking): face construction runs on
    // a VFS worker thread, measurement and outlines on the main thread.
    Mutex ft_lock;
    FlArena* arena;
    FontFace faces[MAX_FONT_FACES];
    FontFace* fallback_face;
    int face_count;
    FlFontId next_id;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlFontCache* font_cache_new(FlArena* arena) {
    FlFontCache* cache = arena_alloc_zero(arena, FlFontCache);
    cache->next_id = 1;
    cache->arena = arena_new();
    mutex_init(&cache->ft_lock);
    FT_Init_FreeType(&cache->library);
    return cache;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Quantize font size to reduce atlas proliferation. Shared with the SW glyph realization layer so the metrics
// measured here and the coverage bitmaps it rasterizes agree on a size.
//   < 30px: 2px steps   30-59px: 4px steps   >= 60px: 8px steps

u16 font_cache_quantize_size(u16 size) {
    if (size < 30) {
        return (size + 1) & ~1; // Round to nearest 2px
    } else if (size < 60) {
        return (size + 2) & ~3; // Round to nearest 4px
    } else {
        return (size + 4) & ~7; // Round to nearest 8px
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Quarter-pixel size key for the fractional-size text path: round(size × 4). The same key drives measurement
// and the styled draw so both agree on one FreeType size.

u16 font_cache_quantize_size_q4(float size) {
    if (!(size > 0.0f)) {
        return 0;
    }
    float key = size * 4.0f + 0.5f;
    return key >= 65535.0f ? 65535 : (u16)key;
}

// The whole-px layout path expressed as a quarter-px key: atlas-bucket quantize first, then scale into
// the shared quarter-px key space.
u16 font_cache_whole_px_to_q4(u16 font_size) {
    u32 key = (u32)font_cache_quantize_size(font_size) * 4;
    return key > 65535 ? 65535 : (u16)key;
}

u16 font_cache_q4_to_whole_px(u16 size_q4) {
    u16 px = (u16)((size_q4 + 2) / 4);
    return px < 1 ? 1 : px;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Copy the font file bytes into the face arena and stand up the main-thread metrics face. The bytes are
// owned by the face for its lifetime; FreeType and the SW backend's raster faces read them.
static bool face_init_freetype_from_bytes(FlFontCache* cache, FontFace* face, const void* data, i64 size,
                                          u16 font_size) {
    face->arena = arena_new();
    face->font_data = arena_alloc_array(face->arena, u8, size);
    face->font_data_size = size;
    memcpy(face->font_data, data, size);

    // This runs on a VFS worker thread; the lock serializes it against main-thread FreeType use of cache->library.
    mutex_lock(&cache->ft_lock);
    FT_Error error = FT_New_Memory_Face(cache->library, face->font_data, size, 0, &face->metrics_face);
    if (!error) {
        FT_Set_Pixel_Sizes(face->metrics_face, 0, font_size);
    }
    mutex_unlock(&cache->ft_lock);

    if (error) {
        log_error("Failed to create metrics face: %s", FT_Error_String(error));
        face->status = FontStatus_Failed;
        return false;
    }

    hashmap_new(&face->advance_cache, face->arena, 256);

    face->status = FontStatus_Ready;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static FlVfsData font_load_callback(void* data, i64 size, void* user_data) {
    FontLoadContext* ctx = (FontLoadContext*)user_data;

    // A failed read calls in with no data; the face must reach a terminal status or pollers wait forever.
    if (!data || size <= 0) {
        ctx->face->status = FontStatus_Failed;
        return (FlVfsData) { .success = false };
    }

    if (!face_init_freetype_from_bytes(ctx->cache, ctx->face, data, size, ctx->font_size)) {
        return (FlVfsData) { .success = false };
    }
    return (FlVfsData) { .data = nullptr, .size = 0, .success = true };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Claim the next face slot and stamp the fields every load path sets identically. Returns nullptr when the
// cache is full. A slot stays consumed once claimed, even if the caller's load then fails: the face is left
// at FontStatus_Failed and destroy frees whatever the partial load allocated. FontStatus_Loading is the zero value.

static FontFace* alloc_face(FlFontCache* self, FlFontRasterKind kind, u16 font_size) {
    if (self->face_count >= MAX_FONT_FACES) {
        log_error("Font cache is full, cannot load more fonts");
        return nullptr;
    }

    FontFace* face = &self->faces[self->face_count++];
    *face = (FontFace) {
        .kind = kind,
        .id = self->next_id++,
        .vfs_handle = FL_VFS_HANDLE_INVALID,
        .size = font_size,
    };
    return face;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlFontId font_cache_load_font(FlFontCache* self, FlVfsMount* mount, FlString relative_path, u16 font_size) {
    FontFace* face = alloc_face(self, FlFontRasterKind_FreeType, font_size);
    if (face == nullptr) {
        return FONT_ID_INVALID;
    }

    // Allocated from the cache arena so it outlives this call and reaches the async callback.
    FontLoadContext* ctx = arena_alloc(self->arena, FontLoadContext);
    ctx->cache = self;
    ctx->face = face;
    ctx->font_size = font_size;

    face->vfs_handle = vfs_mount_read_all(mount, relative_path, .callback = font_load_callback, .user_data = ctx);

    if (face->vfs_handle == FL_VFS_HANDLE_INVALID) {
        log_error("Failed to start async font load for %S", relative_path);
        face->status = FontStatus_Failed;
        return FONT_ID_INVALID;
    }

    return face->id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Load a FreeType font from memory: same face shape as the VFS path, ready synchronously.

FlFontId font_cache_load_font_memory(FlFontCache* self, const u8* data, i64 size, u16 font_size) {
    if (data == nullptr || size <= 0) {
        log_error("font_cache_load_font_memory: invalid font data");
        return FONT_ID_INVALID;
    }
    FontFace* face = alloc_face(self, FlFontRasterKind_FreeType, font_size);
    if (face == nullptr) {
        return FONT_ID_INVALID;
    }

    if (!face_init_freetype_from_bytes(self, face, data, size, font_size)) {
        return FONT_ID_INVALID;
    }
    return face->id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool font_cache_load_fallback(FlFontCache* self, FlVfsMount* mount, FlString relative_path) {
    if (self->fallback_face != nullptr) {
        log_warning("Fallback font already loaded, ignoring new load request");
        return true;
    }

    FlFontId fallback_id = font_cache_load_font(self, mount, relative_path, 16);
    if (fallback_id == FONT_ID_INVALID) {
        log_error("Failed to load fallback font: %S", relative_path);
        return false;
    }

    font_cache_set_fallback_font(self, fallback_id);
    if (self->fallback_face == nullptr) {
        return false;
    }
    log_info("Loaded fallback font: %S", relative_path);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlFontId font_cache_load_default_font(FlFontCache* self, u16 scale) {
    FontFace* face = alloc_face(self, FlFontRasterKind_Default, 8 * scale); // Base size is 8x8 pixels
    if (face == nullptr) {
        return FONT_ID_INVALID;
    }

    face->status = FontStatus_Ready;
    face->arena = arena_new();
    face->default_scale = scale;

    return face->id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FontFace* find_face_by_id(FlFontCache* cache, FlFontId font_id) {
    for_count(i, cache->face_count) {
        if (cache->faces[i].id == font_id) {
            return &cache->faces[i];
        }
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void font_cache_set_fallback_font(FlFontCache* self, FlFontId font_id) {
    FontFace* face = find_face_by_id(self, font_id);
    if (face != nullptr) {
        self->fallback_face = face;
        // Cached advances for primary-missing glyphs depend on the fallback face; invalidate every face's
        // advance memoization.
        for_count(i, self->face_count) {
            self->faces[i].ascii_advance_size = 0;
            if (self->faces[i].advance_cache.base.buckets != nullptr) {
                hashmap_clear(&self->faces[i].advance_cache);
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FontStatus font_cache_get_font_status(FlFontCache* self, FlFontId font_id) {
    FontFace* face = find_face_by_id(self, font_id);
    return face != nullptr ? face->status : FontStatus_Failed;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void font_cache_destroy(FlFontCache* self) {
    FL_VALIDATE(self != nullptr);

    for_count(i, self->face_count) {
        FontFace* face = &self->faces[i];

        if (face->vfs_handle != FL_VFS_HANDLE_INVALID) {
            // fl_vfs_close does not wait for a running read, and its callback writes into face and the context in
            // self->arena, both destroyed below.
            fl_vfs_wait(face->vfs_handle);
            fl_vfs_close(face->vfs_handle);
            face->vfs_handle = FL_VFS_HANDLE_INVALID;
        }

        if (face->metrics_face) {
            FT_Done_Face(face->metrics_face);
        }
        if (face->arena) {
            fl_arena_destroy(face->arena);
        }
    }

    if (self->library) {
        FT_Done_FreeType(self->library);
        self->library = nullptr;
    }
    mutex_destroy(&self->ft_lock);
    if (self->arena) {
        fl_arena_destroy(self->arena);
        self->arena = nullptr;
    }

    self->face_count = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void font_cache_update(FlFontCache* self) {
    for_count(i, self->face_count) {
        FontFace* face = &self->faces[i];

        if (face->status == FontStatus_Loading && face->vfs_handle != FL_VFS_HANDLE_INVALID) {
            if (fl_vfs_is_ready(face->vfs_handle)) {
                FlVfsData data = fl_vfs_get_data(face->vfs_handle);

                if (!data.success) {
                    log_error("Font loading failed for FlFontId %d", face->id);
                    face->status = FontStatus_Failed;
                } else {
                    log_info("Font loaded successfully for FlFontId %d", face->id);
                }

                fl_vfs_close(face->vfs_handle);
                face->vfs_handle = FL_VFS_HANDLE_INVALID;
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void font_cache_wait_loaded(FlFontCache* self, FlFontId id) {
    while (font_cache_get_font_status(self, id) == FontStatus_Loading) {
        font_cache_update(self);
        sleep_us(500);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Opt-in load barrier. Pump I/O until no font is still loading, then report whether all succeeded. Waits on
// metrics-readiness only; glyph rasterization stays render-when-ready. A missing/unreadable file resolves to
// FontStatus_Failed and exits the loop (no timeout). Drains only fonts.

bool font_cache_ensure_all_loaded(FlFontCache* self) {
    for (;;) {
        bool any_loading = false;
        for_count(i, self->face_count) {
            if (self->faces[i].status == FontStatus_Loading) {
                any_loading = true;
                break;
            }
        }
        if (!any_loading) {
            break;
        }
        font_cache_update(self);
        sleep_us(500);
    }

    bool all_ready = true;
    for_count(i, self->face_count) {
        if (self->faces[i].status == FontStatus_Failed) {
            all_ready = false;
        }
    }
    return all_ready;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int font_cache_get_size(FlFontCache* self, FlFontId font_id) {
    FontFace* face = find_face_by_id(self, font_id);
    return face != nullptr ? face->size : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Size a metrics face to a quarter-px key: 26.6 nominal height q4 << 4 at 72 dpi, the same FT_Request_Size
// that FT_Set_Pixel_Sizes issues when the key is a whole pixel; fractional keys size at fractional ppem.
static void ft_set_size_q4(FT_Face face, u16 q4) {
    FT_Set_Char_Size(face, 0, (FT_F26Dot6)q4 << 4, 72, 72);
}

// The raw FreeType advance for a single codepoint, in 26.6 fixed point, recomputed only on a cache miss. The
// face/index pair comes from font_cache_resolve_glyph, so the advance is measured on the face a backend takes its
// outline from (the fallback face for codepoints the primary lacks).
static int ft_compute_advance(FlFontCache* self, FontFace* face, u16 q4, u32 codepoint) {
    FontGlyphRef glyph = font_cache_resolve_glyph(self, face->id, codepoint);
    if (glyph.glyph_index == 0) {
        return 0;
    }
    FontFace* resolved = find_face_by_id(self, glyph.font);
    if (resolved == nullptr || resolved->metrics_face == nullptr) {
        return 0;
    }

    mutex_lock_auto(&self->ft_lock);
    FT_Face mf = resolved->metrics_face;
    ft_set_size_q4(mf, q4);
    if (FT_Load_Glyph(mf, glyph.glyph_index, FT_LOAD_NO_BITMAP | FT_LOAD_COMPUTE_METRICS) != 0) {
        return 0;
    }
    return (int)mf->glyph->advance.x;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cached advance for one codepoint at a quarter-px size key. Only the whole-px measure paths may claim the
// dense ASCII row (may_claim_row), so row ownership is deterministic regardless of whether a fractional caller
// touches the face first; the q4-keyed paths always take the hashmap.
static int glyph_advance_q4(FlFontCache* self, FontFace* face, u16 q4, u32 codepoint, bool may_claim_row) {
    switch (face->kind) {
        case FlFontRasterKind_FreeType: {
            if (face->status != FontStatus_Ready || face->metrics_face == nullptr || q4 == 0) {
                return 0;
            }

            if (codepoint < 128) {
                if (may_claim_row && face->ascii_advance_size != q4) {
                    face->ascii_advance_size = q4;
                    for (int i = 0; i < 128; i++) {
                        face->ascii_advance[i] = -1;
                    }
                }
                if (face->ascii_advance_size == q4) {
                    if (face->ascii_advance[codepoint] < 0) {
                        face->ascii_advance[codepoint] = ft_compute_advance(self, face, q4, codepoint);
                    }
                    return face->ascii_advance[codepoint];
                }
            }

            u64 key = ((u64)q4 << 21) | codepoint;
            int* hit = hashmap_get(&face->advance_cache, key);
            if (hit != nullptr) {
                return *hit;
            }
            int adv = ft_compute_advance(self, face, q4, codepoint);
            hashmap_insert(&face->advance_cache, key, adv);
            return adv;
        }
        case FlFontRasterKind_Default:
            if (codepoint < 128) {
                return (8 * face->default_scale) * 64;
            }
            return 0;
        case FlFontRasterKind_None:
        default:
            return 0;
    }
}

int font_cache_get_glyph_advance(FlFontCache* self, FlFontId font_id, u16 font_size, u32 codepoint) {
    FontFace* face = find_face_by_id(self, font_id);
    if (face == nullptr) {
        return 0;
    }
    // Quantize only for FreeType faces: the built-in font ignores size.
    if (face->kind != FlFontRasterKind_FreeType) {
        return glyph_advance_q4(self, face, 0, codepoint, false);
    }
    return glyph_advance_q4(self, face, font_cache_whole_px_to_q4(font_size), codepoint, true);
}

int font_cache_get_glyph_advance_q4(FlFontCache* self, FlFontId font_id, u16 size_q4, u32 codepoint) {
    FontFace* face = find_face_by_id(self, font_id);
    if (face == nullptr) {
        return 0;
    }
    return glyph_advance_q4(self, face, size_q4, codepoint, false);
}

// font_size <= 0 falls back to the face's load size.
static float resolve_size_px(const FontFace* face, float font_size) {
    return font_size > 0.0f ? font_size : (float)face->size;
}

static u16 resolve_size_q4(const FontFace* face, float font_size) {
    return font_cache_quantize_size_q4(resolve_size_px(face, font_size));
}

float font_cache_resolve_size_px(FlFontCache* self, FlFontId font_id, float font_size, float scale) {
    FontFace* face = find_face_by_id(self, font_id);
    if (face == nullptr || scale <= 0.0f) {
        return 0.0f;
    }
    const float size = resolve_size_px(face, font_size) * scale;
    return size > 0.0f ? size : 0.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool font_cache_metrics_ready(FlFontCache* self, FlFontId font_id) {
    FontFace* face = find_face_by_id(self, font_id);
    return face != nullptr && face->status == FontStatus_Ready;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-size vertical metrics, in 26.6 ints. count is the readiness flag (0 = not ready, skip the run this
// frame). 26.6 values are exact in f32, so the float round trip through face_vmetrics is lossless.

static FlFontMetrics face_vmetrics(FlFontCache* self, FontFace* face, u16 q4);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
GlyphList font_cache_get_glyph_list(FlFontCache* self, FlFontId font_id, u16 font_size) {
    FontFace* face = find_face_by_id(self, font_id);
    if (face == nullptr || face->status != FontStatus_Ready) {
        return (GlyphList) { 0 };
    }
    if (face->kind == FlFontRasterKind_None) {
        return (GlyphList) { 0 };
    }
    FlFontMetrics vm = face_vmetrics(self, face, font_cache_whole_px_to_q4(font_size));
    return (GlyphList) {
        .count = 128,
        .ascender = (int)(vm.ascent * 64.0f),
        .descender = -(int)(vm.descent * 64.0f),
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared measurement core

static FontFace* face_for_metrics(FlFontCache* self, FlFontId font_id, const char* caller) {
    FontFace* face = find_face_by_id(self, font_id);
    if (face == nullptr) {
        log_error("%s: font %d not found, returning zeroed metrics - UI layout will be broken", caller, font_id);
        return nullptr;
    }
    if (face->status != FontStatus_Ready) {
        log_error("%s: font %d metrics not ready (status=%d), returning zeroed metrics - UI layout will be broken",
                  caller, font_id, face->status);
        return nullptr;
    }
    return face;
}

// Per-kind vertical line metrics at a quarter-px size key.
static FlFontMetrics face_vmetrics(FlFontCache* self, FontFace* face, u16 q4) {
    FlFontMetrics out = { 0 };
    switch (face->kind) {
        case FlFontRasterKind_FreeType: {
            FT_Face mf = face->metrics_face;
            if (mf != nullptr && q4 != 0) {
                mutex_lock_auto(&self->ft_lock);
                ft_set_size_q4(mf, q4);
                out.ascent = (float)mf->size->metrics.ascender / 64.0f;
                out.descent = (float)-mf->size->metrics.descender / 64.0f;
                out.line_height = (float)mf->size->metrics.height / 64.0f;
            }
            break;
        }
        case FlFontRasterKind_Default:
            // The built-in font's baseline sits at the cell top (zero ascender/descender); only the cell
            // height is meaningful.
            out.line_height = (float)(8 * face->default_scale);
            break;
        case FlFontRasterKind_None:
        default:
            break;
    }
    return out;
}

// Advance sum (26.6) + glyph count over one line, via the shared per-glyph cache.
static i64 sum_advances_q4(FlFontCache* self, FontFace* face, FlString text, u16 q4, bool may_claim_row,
                           i64* out_glyphs) {
    i64 sum = 0;
    i64 glyphs = 0;
    if (text.data != nullptr) {
        if (text.is_ascii) {
            for (size_t j = 0, count = text.length; j < count; j++) {
                sum += glyph_advance_q4(self, face, q4, (unsigned char)text.data[j], may_claim_row);
            }
            glyphs = (i64)text.length;
        } else {
            Utf8Iter it = utf8_iter(text);
            while (!utf8_iter_done(&it)) {
                sum += glyph_advance_q4(self, face, q4, utf8_iter_next(&it), may_claim_row);
                glyphs++;
            }
        }
    }
    *out_glyphs = glyphs;
    return sum;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Vec2 font_cache_get_text_size(FlFontCache* self, FlString text, FlFontId font_id, u16 font_size) {
    FontFace* face = face_for_metrics(self, font_id, "font_cache_get_text_size");
    if (face == nullptr) {
        return (Vec2) { 0, 0 };
    }

    if (text.length == 0 || text.data == nullptr) {
        return (Vec2) { 0, 0 };
    }

    u16 q4 = font_cache_whole_px_to_q4(font_size);
    i64 glyphs = 0;
    float width = (float)sum_advances_q4(self, face, text, q4, true, &glyphs) / 64.0f;
    return (Vec2) { width, face_vmetrics(self, face, q4).line_height };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fractional-size single-line measurement (quarter-px quantized). Letter spacing adds n−1 gaps only (no
// trailing gap), so a measured pill hugs its label; the styled draw applies the same rule.

FlTextMetrics font_cache_measure_text(FlFontCache* self, FlString text, FlFontId font_id, float font_size,
                                      float letter_spacing) {
    FlTextMetrics out = { 0 };

    FontFace* face = face_for_metrics(self, font_id, "font_cache_measure_text");
    if (face == nullptr) {
        return out;
    }

    // A zero key (face loaded at size 0, measured with font_size <= 0) has no usable FreeType size: zeroed metrics.
    u16 q4 = resolve_size_q4(face, font_size);
    if (face->kind == FlFontRasterKind_FreeType && q4 == 0) {
        return out;
    }

    i64 glyphs = 0;
    out.width = (float)sum_advances_q4(self, face, text, q4, false, &glyphs) / 64.0f;
    if (glyphs > 1) {
        out.width += letter_spacing * (float)(glyphs - 1);
    }

    FlFontMetrics vm = face_vmetrics(self, face, q4);
    out.height = vm.line_height;
    out.ascent = vm.ascent;
    out.descent = vm.descent;
    return out;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// String-independent vertical metrics at a fractional size - what font_cache_measure_text reports minus the
// string.

FlFontMetrics font_cache_get_font_metrics(FlFontCache* self, FlFontId font_id, float font_size) {
    FontFace* face = face_for_metrics(self, font_id, "font_cache_get_font_metrics");
    if (face == nullptr) {
        return (FlFontMetrics) { 0 };
    }

    return face_vmetrics(self, face, resolve_size_q4(face, font_size));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Raster-source hand-off: the upstream inputs a backend needs to realize coverage; never coverage itself.

bool font_cache_get_raster_source(FlFontCache* self, FlFontId font_id, FlFontRasterSource* out) {
    FontFace* face = find_face_by_id(self, font_id);
    if (face == nullptr) {
        return false;
    }

    switch (face->kind) {
        case FlFontRasterKind_FreeType:
            if (face->status != FontStatus_Ready || face->font_data == nullptr) {
                return false; // Not loaded yet - the SW backend retries on a later frame.
            }
            *out = (FlFontRasterSource) {
                .kind = FlFontRasterKind_FreeType,
                .font_data = face->font_data,
                .font_data_size = face->font_data_size,
            };
            return true;
        case FlFontRasterKind_Default:
            *out = (FlFontRasterSource) { .kind = FlFontRasterKind_Default, .default_scale = face->default_scale };
            return true;
        case FlFontRasterKind_None:
        default:
            return false;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool font_cache_get_fallback_raster_source(FlFontCache* self, FlFontRasterSource* out) {
    if (self->fallback_face == nullptr) {
        return false;
    }
    return font_cache_get_raster_source(self, self->fallback_face->id, out);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Glyph-outline service. FT_Outline_Decompose walks the loaded outline; the same callbacks run twice - a
// counting pass, then a fill pass into the caller's arena. FreeType closes contours implicitly, so a contour
// whose last segment does not end on its start point gets its closing edge materialized as a Line segment
// (the segment array is consumed as an independent-edge soup by winding math).

typedef struct OutlineDecomposeCtx {
    FlGlyphSegment* segments; // nullptr in the counting pass
    u32 count;
    FlVec2 current;       // last on-curve point, pixels
    FlVec2 contour_start; // first point of the open contour
    bool has_contour;
} OutlineDecomposeCtx;

static FlVec2 outline_point(const FT_Vector* v) {
    return (FlVec2) { (f32)v->x / 64.0f, (f32)v->y / 64.0f }; // 26.6 -> float pixels
}

static void outline_emit(OutlineDecomposeCtx* ctx, FlGlyphSegment seg) {
    if (ctx->segments != nullptr) {
        ctx->segments[ctx->count] = seg;
    }
    ctx->count++;
}

// 26.6 inputs are exact in f32, so the open-contour check can compare exactly.
static void outline_close_contour(OutlineDecomposeCtx* ctx) {
    if (ctx->has_contour && (ctx->current.x != ctx->contour_start.x || ctx->current.y != ctx->contour_start.y)) {
        outline_emit(ctx, (FlGlyphSegment) {
                              .kind = FlGlyphSegment_Line,
                              .pts = { ctx->current, ctx->contour_start },
                          });
    }
    ctx->has_contour = false;
}

static int outline_move_to(const FT_Vector* to, void* user) {
    OutlineDecomposeCtx* ctx = user;
    outline_close_contour(ctx);
    ctx->current = outline_point(to);
    ctx->contour_start = ctx->current;
    ctx->has_contour = true;
    return 0;
}

// The one body behind FreeType's three curve callbacks: a segment runs from the pen through however many
// control points the degree carries to to, and leaves the pen on to. Absent controls are passed as nullptr.
static int outline_seg(OutlineDecomposeCtx* ctx, FlGlyphSegmentKind kind, const FT_Vector* control1,
                       const FT_Vector* control2, const FT_Vector* to) {
    FlGlyphSegment seg = { .kind = kind, .pts = { ctx->current } };
    u32 n = 1;
    if (control1 != nullptr) {
        seg.pts[n++] = outline_point(control1);
    }
    if (control2 != nullptr) {
        seg.pts[n++] = outline_point(control2);
    }
    seg.pts[n] = outline_point(to);

    ctx->current = seg.pts[n];
    outline_emit(ctx, seg);
    return 0;
}

static int outline_line_to(const FT_Vector* to, void* user) {
    return outline_seg(user, FlGlyphSegment_Line, nullptr, nullptr, to);
}

static int outline_conic_to(const FT_Vector* control, const FT_Vector* to, void* user) {
    return outline_seg(user, FlGlyphSegment_Quadratic, control, nullptr, to);
}

static int outline_cubic_to(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user) {
    return outline_seg(user, FlGlyphSegment_Cubic, control1, control2, to);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 face_glyph_index(FlFontCache* self, FontFace* face, u32 codepoint) {
    if (face == nullptr || face->kind != FlFontRasterKind_FreeType || face->status != FontStatus_Ready
        || face->metrics_face == nullptr) {
        return 0;
    }
    mutex_lock_auto(&self->ft_lock);
    return (u32)FT_Get_Char_Index(face->metrics_face, codepoint);
}

u32 font_cache_get_glyph_index(FlFontCache* self, FlFontId font_id, u32 codepoint) {
    return face_glyph_index(self, find_face_by_id(self, font_id), codepoint);
}

// Primary first, fallback face second - the same order ft_compute_advance takes for metrics, so a glyph's
// outline and its advance always come from one face.
FontGlyphRef font_cache_resolve_glyph(FlFontCache* self, FlFontId font_id, u32 codepoint) {
    FontGlyphRef ref
        = { .font = font_id, .glyph_index = face_glyph_index(self, find_face_by_id(self, font_id), codepoint) };
    if (ref.glyph_index != 0 || self->fallback_face == nullptr || self->fallback_face->id == font_id) {
        return ref;
    }

    u32 fallback_index = face_glyph_index(self, self->fallback_face, codepoint);
    if (fallback_index == 0) {
        return ref; // neither face maps it; the caller falls through to the primary's .notdef
    }
    return (FontGlyphRef) { .font = self->fallback_face->id, .glyph_index = fallback_index };
}

bool font_cache_get_glyph_outline(FlFontCache* self, FlFontId font_id, u16 size_q4, u32 glyph_id, struct FlArena* arena,
                                  FlGlyphOutline* out) {
    FontFace* face = find_face_by_id(self, font_id);
    if (face == nullptr || face->kind != FlFontRasterKind_FreeType || face->status != FontStatus_Ready
        || face->metrics_face == nullptr || size_q4 == 0) {
        return false;
    }

    FT_Face mf = face->metrics_face;
    // Held across the whole extraction: the size request, glyph load, both decompose passes, and the CBox/
    // advance reads all touch shared per-face and library state that a worker-thread face build could corrupt.
    mutex_lock_auto(&self->ft_lock);
    ft_set_size_q4(mf, size_q4);

    // Light hinting: the autohinter with vertical stem snapping only, evaluated at the target pixel size.
    if (FT_Load_Glyph(mf, glyph_id, FT_LOAD_NO_BITMAP | FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_LIGHT) != 0) {
        return false;
    }
    if (mf->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
        return false;
    }
    FT_Outline* outline = &mf->glyph->outline;

    static const FT_Outline_Funcs funcs = {
        .move_to = outline_move_to,
        .line_to = outline_line_to,
        .conic_to = outline_conic_to,
        .cubic_to = outline_cubic_to,
    };

    OutlineDecomposeCtx count_pass = { 0 };
    if (FT_Outline_Decompose(outline, &funcs, &count_pass) != 0) {
        return false;
    }
    outline_close_contour(&count_pass);

    FlGlyphSegment* segments = nullptr;
    if (count_pass.count > 0) {
        segments = arena_alloc_array(arena, FlGlyphSegment, count_pass.count);
        OutlineDecomposeCtx fill_pass = { .segments = segments };
        FT_Outline_Decompose(outline, &funcs, &fill_pass);
        outline_close_contour(&fill_pass);
    }

    FT_BBox cbox; // control box: cheap, conservative for curves - cell padding absorbs the overestimate
    FT_Outline_Get_CBox(outline, &cbox);
    bool empty = count_pass.count == 0;

    *out = (FlGlyphOutline) {
        .segments = segments,
        .segment_count = count_pass.count,
        .advance = (f32)mf->glyph->advance.x / 64.0f,
        .min_x = empty ? 0.0f : (f32)cbox.xMin / 64.0f,
        .min_y = empty ? 0.0f : (f32)cbox.yMin / 64.0f,
        .max_x = empty ? 0.0f : (f32)cbox.xMax / 64.0f,
        .max_y = empty ? 0.0f : (f32)cbox.yMax / 64.0f,
    };
    return true;
}
