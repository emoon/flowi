// image decode and compose tests

#include "render/common.h"

// The Tint background mode composites the decoded image right-aligned onto a wider canvas filled with the tint
// color, then blends a short tint-to-image gradient at the left seam. The right-alignment offset
// (canvas_w - image_width) is used by both the canvas blit and the gradient seam: the source is flush against
// the right edge, with pure tint to the left.
UTEST(image_decode, tint_mode_right_aligns_source) {
    // A reference decode with no canvas gives the source pixels. k_png_red is a 4x4 RGB PNG.
    ImageDecodeOptions ref_opts = { .ui_scale = 1.0f };
    FlImageDecodeResult* src = image_decode(s_arena, k_png_red, sizeof(k_png_red), &ref_opts);
    ASSERT_TRUE_MSG(src != nullptr, "reference decode of k_png_red failed");
    ASSERT_EQ_MSG((int)src->channels, 3, "RGB source");
    ASSERT_EQ_MSG(src->width, 4, "source is 4px wide");

    // Tint onto an 8x4 canvas. Keeping the aspect ratio leaves the 4x4 image 4px wide, so it right-aligns at x=4.
    ImageDecodeOptions opts = {
        .ui_scale = 1.0f,
        .target_width = 8,
        .target_height = 4,
        .preserve_aspect_ratio = true,
    };
    ImageThumbnailOptions thumb = {
        .background_mode = ImageDecodeBackgroundMode_Tint,
        .tint_r = 10,
        .tint_g = 200,
        .tint_b = 30,
    };
    FlImageDecodeResult* out = image_decode(s_arena, k_png_red, sizeof(k_png_red), &opts);
    ASSERT_TRUE_MSG(out != nullptr, "tint decode failed");
    ASSERT_TRUE_MSG(out->needs_thumbnail_compose, "raster decode is raw, pending the compose stage");
    image_thumbnail_compose(s_arena, out, &opts, &thumb);
    ASSERT_FALSE_MSG(out->needs_thumbnail_compose, "compose clears the pending flag");
    const IntCase canvas[] = {
        { out->width, 8, "output spans the full canvas width" },
        { out->height, 4, "output spans the full canvas height" },
        { (int)out->channels, 3, "tint canvas is RGB" },
    };
    check_int_cases(utest_result, canvas, sizeof_array(canvas));

    // The left fill region (x < image_start_x = 4) is pure tint color, which the gradient seam does not touch. The
    // rightmost canvas column (x=7) maps to source column 3 and is past the gradient seam, so it equals the source
    // pixel exactly.
    const u8* p_left = out->pixels;            // (0,0)
    const u8* p_right = out->pixels + 7 * 3;   // (7,0)
    const u8* src_right = src->pixels + 3 * 3; // source (3,0)
    const IntCase columns[] = {
        { (int)p_left[0], 10, "left column is tint red" },
        { (int)p_left[1], 200, "left column is tint green" },
        { (int)p_left[2], 30, "left column is tint blue" },
        { (int)p_right[0], (int)src_right[0], "right column keeps source red" },
        { (int)p_right[1], (int)src_right[1], "right column keeps source green" },
        { (int)p_right[2], (int)src_right[2], "right column keeps source blue" },
    };
    check_int_cases(utest_result, columns, sizeof_array(columns));
}

// Center background mode letterboxes the source, with its aspect ratio kept, onto a canvas of the fill color. A
// 4x4 source on an 8x4 canvas stays 4px wide and is centered at offset_x=2, which leaves black fill columns at
// the edges.
UTEST(image_decode, center_mode_letterboxes_source) {
    ImageDecodeOptions ref_opts = { .ui_scale = 1.0f };
    FlImageDecodeResult* src = image_decode(s_arena, k_png_red, sizeof(k_png_red), &ref_opts);
    ASSERT_TRUE_MSG(src != nullptr, "reference decode of k_png_red failed");
    ASSERT_EQ_MSG(src->width, 4, "source is 4px wide");

    ImageDecodeOptions opts = {
        .ui_scale = 1.0f,
        .target_width = 8,
        .target_height = 4,
        .preserve_aspect_ratio = true,
    };
    ImageThumbnailOptions thumb = {
        .background_mode = ImageDecodeBackgroundMode_Center,
        .tint_r = 0, // Center fills the letterbox margins with black, as the caller does
        .tint_g = 0,
        .tint_b = 0,
    };
    FlImageDecodeResult* out = image_decode(s_arena, k_png_red, sizeof(k_png_red), &opts);
    ASSERT_TRUE_MSG(out != nullptr, "center decode failed");
    image_thumbnail_compose(s_arena, out, &opts, &thumb);
    ASSERT_EQ_MSG(out->width, 8, "output spans the full canvas width");
    ASSERT_EQ_MSG(out->height, 4, "output spans the full canvas height");
    ASSERT_EQ_MSG((int)out->channels, 3, "center canvas is RGB");

    // The left margin (x=0, before the centered source at x=2) is the black fill color.
    const u8* p_left = out->pixels; // (0,0)
    ASSERT_EQ_MSG((int)p_left[0], 0, "left margin is black fill R");
    ASSERT_EQ_MSG((int)p_left[1], 0, "left margin is black fill G");
    ASSERT_EQ_MSG((int)p_left[2], 0, "left margin is black fill B");

    // Canvas column 3 is inside the centered source (offset_x=2) and maps to source column 1.
    const u8* p_mid = out->pixels + 3 * 3;
    const u8* src_mid = src->pixels + 1 * 3;
    ASSERT_EQ_MSG((int)p_mid[0], (int)src_mid[0], "centered source keeps red");
    ASSERT_EQ_MSG((int)p_mid[1], (int)src_mid[1], "centered source keeps green");
    ASSERT_EQ_MSG((int)p_mid[2], (int)src_mid[2], "centered source keeps blue");
}

// A 4x4 binary grayscale PGM. stb's PNM decoder returns P5 as a single channel, which the compose path has to
// handle.
static const u8 k_pgm_gray4x4[] = {
    'P',  '5',  '\n', '4',  ' ', '4', '\n', '2', '5', '5', '\n', // header: P5 4x4 maxval 255
    0x10, 0x20, 0x30, 0x40,                                      // row 0
    0x50, 0x60, 0x70, 0x80,                                      // row 1
    0x90, 0xA0, 0xB0, 0xC0,                                      // row 2
    0xD0, 0xE0, 0xF0, 0xFF,                                      // row 3
};

// compose_source_into_canvas_rgb does not read past a source pixel's channel data. For a 1-channel source it
// replicates channel 0 into R, G and B. The source is exactly width*height bytes, so R==G==B==gray also shows
// that no neighbouring byte ended up in G or B.
UTEST(image_decode, compose_grayscale_source_replicates_gray) {
    const u8 gray[6] = { 11, 22, 33, 44, 55, 66 }; // 3x2, 1 channel, stride 3
    FlImageDecodeResult img = {
        .pixels = (u8*)gray,
        .width = 3,
        .height = 2,
        .stride = 3,
        .channels = 1,
    };

    u8 canvas[3 * 2 * 3];
    const ImageCanvas target = { .pixels = canvas, .width = 3, .height = 2, .fill = { 5, 5, 5 } };
    composite_source_into_canvas_rgb(&img, &target);

    for (int i = 0; i < 3 * 2; i++) {
        ASSERT_EQ_MSG((int)canvas[i * 3 + 0], (int)gray[i], "grayscale value replicated into R");
        ASSERT_EQ_MSG((int)canvas[i * 3 + 1], (int)gray[i], "grayscale value replicated into G");
        ASSERT_EQ_MSG((int)canvas[i * 3 + 2], (int)gray[i], "grayscale value replicated into B");
    }
}

// The same for a 2-channel (grayscale and alpha) source: the gray value is channel 0, and alpha, channel 1, is
// dropped and not read into G or B. The source is exactly width*height*2 bytes.
UTEST(image_decode, compose_gray_alpha_source_ignores_alpha) {
    const u8 ga[6] = { 100, 255, 150, 128, 200, 0 }; // 3x1, 2 channels (gray, alpha), stride 6
    FlImageDecodeResult img = {
        .pixels = (u8*)ga,
        .width = 3,
        .height = 1,
        .stride = 6,
        .channels = 2,
    };

    u8 canvas[3 * 1 * 3];
    const ImageCanvas target = { .pixels = canvas, .width = 3, .height = 1 };
    composite_source_into_canvas_rgb(&img, &target);

    const int expected_gray[3] = { 100, 150, 200 }; // channel 0 of each pixel, with alpha ignored
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ_MSG((int)canvas[i * 3 + 0], expected_gray[i], "gray-alpha: channel 0 into R");
        ASSERT_EQ_MSG((int)canvas[i * 3 + 1], expected_gray[i], "gray-alpha: channel 0 into G (not alpha)");
        ASSERT_EQ_MSG((int)canvas[i * 3 + 2], expected_gray[i], "gray-alpha: channel 0 into B (not alpha)");
    }
}

// The black-border predicates handle 2-channel rows. stb returns 2 channels for a grey+alpha PNG when force_rgb
// is off, so a GA row that is not black reports false from its grey byte, and a high alpha byte does not make a
// black row report non-black.
UTEST(image_decode, black_predicates_read_gray_alpha_channel) {
    const u8 bright_ga[6] = { 200, 0, 200, 0, 200, 0 }; // 3x1 GA: bright grey, zero alpha
    const FlImageDecodeResult bright
        = { .pixels = (u8*)bright_ga, .width = 3, .height = 1, .stride = 6, .channels = 2 };
    ASSERT_FALSE_MSG(image_region_all_black(&bright, (PixelRect) { 0, 1, 0, 3 }),
                     "a bright grey+alpha row must not report all-black");
    ASSERT_FALSE_MSG(image_region_all_black(&bright, (PixelRect) { 0, 1, 1, 2 }),
                     "a bright grey+alpha column must not report all-black");

    const u8 black_ga[6] = { 0, 255, 0, 255, 0, 255 }; // 3x1 GA: black grey, opaque alpha
    const FlImageDecodeResult black = { .pixels = (u8*)black_ga, .width = 3, .height = 1, .stride = 6, .channels = 2 };
    ASSERT_TRUE_MSG(image_region_all_black(&black, (PixelRect) { 0, 1, 0, 3 }),
                    "a black grey+alpha row must report all-black regardless of alpha");
    ASSERT_TRUE_MSG(image_region_all_black(&black, (PixelRect) { 0, 1, 1, 2 }),
                    "a black grey+alpha column must report all-black regardless of alpha");
}

// When a resize fails to shrink an oversized source, the Tint and Center offsets go negative. The clamped blit
// crops the source to the canvas and writes nothing outside it. Guard bytes on both sides of a 2x2 canvas catch
// an out-of-bounds write.
UTEST(image_decode, compose_oversized_source_clamps_to_canvas) {
    enum { CW = 2, CH = 2, CANVAS_BYTES = CW * CH * 3, GUARD = 8 };

    // A 4x4 RGB source in which each pixel's value comes from its linear index.
    u8 src[4 * 4 * 3];
    for (int i = 0; i < 4 * 4; i++) {
        src[i * 3 + 0] = (u8)(i * 3 + 0);
        src[i * 3 + 1] = (u8)(i * 3 + 1);
        src[i * 3 + 2] = (u8)(i * 3 + 2);
    }
    FlImageDecodeResult img = {
        .pixels = src,
        .width = 4,
        .height = 4,
        .stride = 4 * 3,
        .channels = 3,
    };

    // The guarded buffer is [GUARD 0xAB | canvas | GUARD 0xAB], and canvas points into the middle.
    u8 buf[GUARD + CANVAS_BYTES + GUARD];
    for (int i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = 0xAB;
    }
    u8* canvas = buf + GUARD;

    // Offset (-1,-1) is the Center offset for a source larger than the canvas, (2-4)/2. dst_x = -1 + x, so x==0
    // falls before the canvas and x==3 past it, and both are dropped. x==1 and x==2 map to canvas columns 0 and 1.
    const ImageCanvas target = {
        .pixels = canvas,
        .width = CW,
        .height = CH,
        .fill = { 7, 8, 9 },
        .offset_x = -1,
        .offset_y = -1,
    };
    composite_source_into_canvas_rgb(&img, &target);

    for (int i = 0; i < GUARD; i++) {
        ASSERT_EQ_MSG((int)buf[i], 0xAB, "leading guard byte untouched (no OOB write before canvas)");
        ASSERT_EQ_MSG((int)buf[GUARD + CANVAS_BYTES + i], 0xAB,
                      "trailing guard byte untouched (no OOB write after canvas)");
    }

    // Canvas (col,row) == source (col+1,row+1), so the region kept is the right crop.
    for (int row = 0; row < CH; row++) {
        for (int col = 0; col < CW; col++) {
            int src_idx = (row + 1) * 4 + (col + 1);
            const u8* c = canvas + (row * CW + col) * 3;
            ASSERT_EQ_MSG((int)c[0], (int)src[src_idx * 3 + 0], "cropped source R lands at the clamped offset");
            ASSERT_EQ_MSG((int)c[1], (int)src[src_idx * 3 + 1], "cropped source G lands at the clamped offset");
            ASSERT_EQ_MSG((int)c[2], (int)src[src_idx * 3 + 2], "cropped source B lands at the clamped offset");
        }
    }
}

// The same through a real decode: a 1-channel image (P5 PGM, channels==1) goes through the Center compose path
// without a read past the source.
UTEST(image_decode, compose_grayscale_decode_center_no_overread) {
    ImageDecodeOptions ref_opts = { .ui_scale = 1.0f };
    FlImageDecodeResult* src = image_decode(s_arena, k_pgm_gray4x4, sizeof(k_pgm_gray4x4), &ref_opts);
    ASSERT_TRUE_MSG(src != nullptr, "reference decode of k_pgm_gray4x4 failed");
    ASSERT_EQ_MSG((int)src->channels, 1, "P5 PGM decodes to a single grayscale channel");
    ASSERT_EQ_MSG(src->width, 4, "grayscale source is 4px wide");

    ImageDecodeOptions opts = {
        .ui_scale = 1.0f,
        .target_width = 8,
        .target_height = 4,
        .preserve_aspect_ratio = true,
    };
    ImageThumbnailOptions thumb = {
        .background_mode = ImageDecodeBackgroundMode_Center,
        .tint_r = 1,
        .tint_g = 2,
        .tint_b = 3,
    };
    FlImageDecodeResult* out = image_decode(s_arena, k_pgm_gray4x4, sizeof(k_pgm_gray4x4), &opts);
    ASSERT_TRUE_MSG(out != nullptr, "grayscale center decode failed");
    image_thumbnail_compose(s_arena, out, &opts, &thumb);
    ASSERT_EQ_MSG(out->width, 8, "output spans the full canvas width");
    ASSERT_EQ_MSG((int)out->channels, 3, "compose canvas is RGB");

    // The left margin (x=0, before the centered 4px source at offset_x=2) is the fill color.
    const u8* p_left = out->pixels;
    ASSERT_EQ_MSG((int)p_left[0], 1, "left margin is fill R");
    ASSERT_EQ_MSG((int)p_left[1], 2, "left margin is fill G");
    ASSERT_EQ_MSG((int)p_left[2], 3, "left margin is fill B");

    // Canvas column 2 is the first centered source column, and a grayscale source gives R==G==B there.
    const u8* p_src0 = out->pixels + 2 * 3;
    ASSERT_EQ_MSG((int)p_src0[0], (int)p_src0[1], "centered grayscale pixel: R==G");
    ASSERT_EQ_MSG((int)p_src0[1], (int)p_src0[2], "centered grayscale pixel: G==B");
    ASSERT_EQ_MSG((int)p_src0[0], (int)src->pixels[0], "centered pixel matches decoded gray value");
}

// The vertical fade pass needs a 3-channel image, which force_rgb provides, and blends each row from the original
// color at the top toward the target color at the bottom. With fade_start=0 the top row stays original and the
// bottom row reaches the target color.
UTEST(image_decode, vertical_fade_blends_toward_target) {
    ImageDecodeOptions ref_opts = { .ui_scale = 1.0f };
    FlImageDecodeResult* src = image_decode(s_arena, k_png_red, sizeof(k_png_red), &ref_opts);
    ASSERT_TRUE_MSG(src != nullptr, "reference decode of k_png_red failed");

    ImageDecodeOptions opts = { .ui_scale = 1.0f, .force_rgb = true };
    ImageThumbnailOptions thumb = {
        .apply_vertical_fade = true,
        .fade_start = 0.0f,
        .fade_r = 0,
        .fade_g = 0,
        .fade_b = 255, // fade toward blue
    };
    FlImageDecodeResult* out = image_decode(s_arena, k_png_red, sizeof(k_png_red), &opts);
    ASSERT_TRUE_MSG(out != nullptr, "fade decode failed");
    ASSERT_EQ_MSG((int)out->channels, 3, "force_rgb yields a 3-channel decode for the fade pass");
    image_thumbnail_compose(s_arena, out, &opts, &thumb);
    ASSERT_EQ_MSG((int)out->channels, 3, "faded image stays RGB");

    // The top row keeps the source color and the bottom row is pulled toward the blue target.
    const u8* top = out->pixels;                                      // (0,0)
    const u8* bottom = out->pixels + (out->height - 1) * out->stride; // (0, h-1)
    ASSERT_EQ_MSG((int)top[0], (int)src->pixels[0], "top row keeps the original red");
    ASSERT_TRUE_MSG((int)bottom[2] > (int)top[2], "bottom row blends toward the blue fade target");
}

// The number of RGB pixels in a row whose red channel is neither close to black nor close to white.
static int count_intermediate_reds(const u8* row, int width) {
    int count = 0;
    for (int x = 0; x < width; ++x) {
        count += (row[x * 3] > 40 && row[x * 3] < 215) ? 1 : 0;
    }
    return count;
}

// A 2x2 RGB image whose left column is black and right column white.
static void fill_black_white_columns(u8* pixels) {
    for (int i = 0; i < 4; ++i) {
        const u8 value = (i % 2) == 0 ? 0 : 255;
        pixels[i * 3 + 0] = pixels[i * 3 + 1] = pixels[i * 3 + 2] = value;
    }
}

// ImageDecodeResizeFilter is load-time resampling, chosen per load and applied once in the compose stage. It is
// separate from the draw-time FlImageFilter the render command carries. Upscaling a hard black/white edge tells
// the filters apart: the default filter interpolates across the edge, and point sampling can only reproduce the
// two source values.
UTEST(image_decode, resize_filter_selects_the_load_time_resampler) {
    u8 src_pixels[2 * 2 * 3];
    fill_black_white_columns(src_pixels);

    const ImageDecodeResizeFilter filters[2] = { ImageDecodeResizeFilter_Default, ImageDecodeResizeFilter_Nearest };
    int intermediates[2] = { 0, 0 };

    for (int f = 0; f < 2; ++f) {
        FlImageDecodeResult result = {
            .pixels = src_pixels,
            .width = 2,
            .height = 2,
            .stride = 2 * 3,
            .channels = 3,
            .premultiplied = false,
            .needs_thumbnail_compose = true,
        };
        ImageDecodeOptions opts = {
            .ui_scale = 1.0f,
            .target_width = 8,
            .target_height = 8,
            .resize_filter = filters[f],
        };
        ImageThumbnailOptions thumb = { .background_mode = ImageDecodeBackgroundMode_Regular };
        image_thumbnail_compose(s_arena, &result, &opts, &thumb);

        ASSERT_EQ_MSG(result.width, 8, "compose resized to the requested width");
        ASSERT_EQ_MSG(result.height, 8, "compose resized to the requested height");

        intermediates[f] = count_intermediate_reds(result.pixels + (size_t)4 * result.stride, 8);
    }

    ASSERT_TRUE_MSG(intermediates[0] > 0, "the default resize filter interpolates across the edge");
    ASSERT_EQ_MSG(intermediates[1], 0, "the nearest resize filter point-samples, producing no new values");
}

#define CROP_GRAY_BORDER 2

#define CROP_GRAY_INNER 8

#define CROP_GRAY_SIZE (CROP_GRAY_INNER + 2 * CROP_GRAY_BORDER)

#define CROP_GRAY_VALUE 100

// An RGBA image with an opaque black border around an opaque gray interior. When `odd_last_row` is set, the
// bottom interior row has one colored pixel.
static void fill_black_bordered_gray(u8* pixels, bool odd_last_row) {
    for (int y = 0; y < CROP_GRAY_SIZE; ++y) {
        for (int x = 0; x < CROP_GRAY_SIZE; ++x) {
            bool inside = x >= CROP_GRAY_BORDER && x < CROP_GRAY_SIZE - CROP_GRAY_BORDER && y >= CROP_GRAY_BORDER
                          && y < CROP_GRAY_SIZE - CROP_GRAY_BORDER;
            u8 v = inside ? CROP_GRAY_VALUE : 0;
            u8* px = pixels + ((size_t)y * CROP_GRAY_SIZE + (size_t)x) * 4;
            px[0] = px[1] = px[2] = v;
            px[3] = 255;
        }
    }

    if (odd_last_row) {
        u8* px = pixels + ((size_t)(CROP_GRAY_SIZE - CROP_GRAY_BORDER - 1) * CROP_GRAY_SIZE + CROP_GRAY_BORDER) * 4;
        px[1] = 10;
    }
}

static FlImageDecodeResult compose_black_bordered(u8* pixels, int target) {
    FlImageDecodeResult result = {
        .pixels = pixels,
        .width = CROP_GRAY_SIZE,
        .height = CROP_GRAY_SIZE,
        .stride = CROP_GRAY_SIZE * 4,
        .channels = 4,
        .needs_thumbnail_compose = true,
    };
    ImageDecodeOptions opts = {
        .ui_scale = 1.0f,
        .target_width = target,
        .target_height = target,
        .detect_grayscale = true,
    };
    ImageThumbnailOptions thumb = { .detect_black_border = true };
    image_thumbnail_compose(s_arena, &result, &opts, &thumb);
    return result;
}

// The black-border crop does not copy, so the cropped image keeps the parent's row stride. Grayscale detection and
// conversion walk rows by that stride and do not treat the interior as packed.
UTEST(image_decode, crop_then_grayscale_honors_stride) {
    u8 pixels[CROP_GRAY_SIZE * CROP_GRAY_SIZE * 4];
    fill_black_bordered_gray(pixels, false);

    FlImageDecodeResult result = compose_black_bordered(pixels, 0);

    ASSERT_EQ_MSG(result.width, CROP_GRAY_INNER, "border cropped");
    ASSERT_EQ_MSG(result.height, CROP_GRAY_INNER, "border cropped");
    ASSERT_EQ_MSG((int)result.channels, 1, "gray interior converts to alpha only");
    for (int i = 0; i < CROP_GRAY_INNER * CROP_GRAY_INNER; ++i) {
        ASSERT_TRUE_MSG(abs((int)result.pixels[i] - CROP_GRAY_VALUE) <= 1,
                        "alpha comes from the interior, not the border");
    }
}

UTEST(image_decode, crop_then_color_row_refuses_grayscale) {
    u8 pixels[CROP_GRAY_SIZE * CROP_GRAY_SIZE * 4];
    fill_black_bordered_gray(pixels, true);

    FlImageDecodeResult result = compose_black_bordered(pixels, 0);

    ASSERT_EQ_MSG(result.width, CROP_GRAY_INNER, "border cropped");
    ASSERT_EQ_MSG((int)result.channels, 4, "a colored pixel in the last interior row keeps the image RGBA");
}

UTEST(image_decode, crop_then_resize_grayscale_stays_correct) {
    u8 pixels[CROP_GRAY_SIZE * CROP_GRAY_SIZE * 4];
    fill_black_bordered_gray(pixels, false);

    FlImageDecodeResult result = compose_black_bordered(pixels, 4);

    ASSERT_EQ_MSG(result.width, 4, "resized to the target");
    ASSERT_EQ_MSG((int)result.channels, 1, "gray interior converts to alpha only");
    for (int i = 0; i < 4 * 4; ++i) {
        ASSERT_TRUE_MSG(abs((int)result.pixels[i] - CROP_GRAY_VALUE) <= 1, "resized alpha matches the interior");
    }
}

UTEST(image_decode, checked_pixel_size_rejects_unrepresentable_sizes) {
    size_t size = 0;
    ASSERT_TRUE(image_checked_pixel_size(3, 2, 4, &size));
    ASSERT_EQ((size_t)24, size);
    const BoolCase rejected[] = {
        { image_checked_pixel_size(0, 2, 4, &size), false, "a zero width is unrepresentable" },
        { image_checked_pixel_size(2, -1, 4, &size), false, "a negative height is unrepresentable" },
        { image_checked_pixel_size(2, 2, 0, &size), false, "a zero pixel size is unrepresentable" },
        { image_checked_pixel_size(INT_MAX, INT_MAX, SIZE_MAX / 4, &size), false, "an overflowing size is rejected" },
    };
    check_bool_cases(utest_result, rejected, sizeof_array(rejected));

#if SIZE_MAX > UINT32_MAX
    // Larger than any int product but still representable, so the full byte count comes back.
    ASSERT_TRUE(image_checked_pixel_size(50000, 50000, 4, &size));
    ASSERT_EQ((size_t)10000000000u, size);
#endif

    int stride = 0;
    ASSERT_TRUE(image_checked_stride(INT_MAX / 4, 4, &stride));
    ASSERT_EQ((INT_MAX / 4) * 4, stride);
    ASSERT_FALSE(image_checked_stride(INT_MAX / 4 + 1, 4, &stride));
}

UTEST(image_decode, dynamic_image_refuses_stride_overflow) {
    ASSERT_TRUE(dynamic_image_create(INT_MAX / 4 + 1, 1, s_arena) == nullptr);
    FlDynamicImage* ok = dynamic_image_create(8, 4, s_arena);
    ASSERT_TRUE(ok != nullptr);
    ASSERT_EQ(8 * 4, ok->user_stride);
}

// The public constructors return a zeroed buffer of the right shape and refuse unusable arguments.
UTEST(image_decode, public_dynamic_image_constructors_shape_the_buffer) {
    FlDynamicImage* rgba = fl_dynamic_image_create(5, 3, (FlArena*)s_arena);
    ASSERT_TRUE(rgba != nullptr);
    const IntCase shape[] = {
        { rgba->width, 5, "the width is kept" },
        { rgba->height, 3, "the height is kept" },
        { rgba->user_stride, 5 * 4, "RGBA is four bytes per pixel" },
        { rgba->is_alpha_only, 0, "an RGBA image is not alpha only" },
    };
    check_int_cases(utest_result, shape, sizeof_array(shape));
    for (int i = 0; i < 5 * 3 * 4; i++) {
        ASSERT_EQ_MSG((int)rgba->user_buffer[i], 0, "public RGBA buffer starts zeroed");
    }

    FlDynamicImage* alpha = fl_dynamic_image_create_alpha(5, 3, (FlArena*)s_arena);
    ASSERT_TRUE(alpha != nullptr);
    ASSERT_EQ(5, alpha->user_stride);
    ASSERT_TRUE(alpha->is_alpha_only);

    const BoolCase refused[] = {
        { fl_dynamic_image_create(0, 3, (FlArena*)s_arena) == nullptr, true, "a zero width is refused" },
        { fl_dynamic_image_create_alpha(5, -1, (FlArena*)s_arena) == nullptr, true, "a negative height is refused" },
        { fl_dynamic_image_create(5, 3, nullptr) == nullptr, true, "a missing arena is refused" },
    };
    check_bool_cases(utest_result, refused, sizeof_array(refused));
}

static const char k_svg_declared_huge[]
    = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40000\" height=\"40000\">"
      "<rect x=\"0\" y=\"0\" width=\"40000\" height=\"40000\" fill=\"#ff0000\"/></svg>";

static const char k_svg_at_cap[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8192\" height=\"16\">"
                                   "<rect x=\"0\" y=\"0\" width=\"8192\" height=\"16\" fill=\"#ff0000\"/></svg>";

static const char k_svg_over_cap[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8193\" height=\"16\">"
                                     "<rect x=\"0\" y=\"0\" width=\"8193\" height=\"16\" fill=\"#ff0000\"/></svg>";

// A hostile declared size is rejected before any rasterizer or pixel allocation exists.
UTEST(image_decode, svg_rejects_absurd_declared_size) {
    ImageDecodeOptions opts = { .ui_scale = 1.0f };
    ASSERT_TRUE(image_decode(s_arena, k_svg_declared_huge, sizeof(k_svg_declared_huge) - 1, &opts) == nullptr);
    ASSERT_TRUE(image_decode(s_arena, k_svg_over_cap, sizeof(k_svg_over_cap) - 1, &opts) == nullptr);
}

UTEST(image_decode, svg_at_size_cap_still_decodes) {
    ImageDecodeOptions opts = { .ui_scale = 1.0f };
    FlImageDecodeResult* out = image_decode(s_arena, k_svg_at_cap, sizeof(k_svg_at_cap) - 1, &opts);
    ASSERT_TRUE(out != nullptr);
    ASSERT_EQ(8192, out->width);
}

// The cap applies to the final raster, so a huge declared size that a target scales down still decodes.
UTEST(image_decode, svg_huge_declared_size_with_small_target_decodes) {
    ImageDecodeOptions opts
        = { .ui_scale = 1.0f, .target_width = 32, .target_height = 32, .preserve_aspect_ratio = true };
    FlImageDecodeResult* out = image_decode(s_arena, k_svg_declared_huge, sizeof(k_svg_declared_huge) - 1, &opts);
    ASSERT_TRUE(out != nullptr);
    ASSERT_EQ(32, out->width);
}

UTEST(image_decode, svg_refuses_oversized_target) {
    ImageDecodeOptions opts = { .ui_scale = 1.0f, .target_width = INT_MAX / 4 + 1, .target_height = 1 };
    ASSERT_TRUE(image_decode(s_arena, k_svg_gradient, sizeof(k_svg_gradient) - 1, &opts) == nullptr);
}

// SVG decodes are final: image_decode leaves needs_thumbnail_compose false, and the compose stage does nothing
// for them even when compositing flags such as detect_black_border are requested.
UTEST(image_decode, svg_skips_thumbnail_compose) {
    ImageDecodeOptions opts = { .ui_scale = 1.0f, .target_width = 16, .target_height = 16 };
    FlImageDecodeResult* out = image_decode(s_arena, k_svg_gradient, sizeof(k_svg_gradient) - 1, &opts);
    ASSERT_TRUE_MSG(out != nullptr, "svg decode failed");
    ASSERT_FALSE_MSG(out->needs_thumbnail_compose, "svg decode is final, no compose pending");

    int w = out->width;
    int h = out->height;
    int ch = out->channels;
    const u8* px = out->pixels;

    ImageThumbnailOptions thumb = { .detect_black_border = true, .background_mode = ImageDecodeBackgroundMode_Center };
    image_thumbnail_compose(s_arena, out, &opts, &thumb);
    const IntCase untouched[] = {
        { out->width, w, "compose left svg width untouched" },
        { out->height, h, "compose left svg height untouched" },
        { (int)out->channels, ch, "compose left svg channels untouched" },
        { out->pixels == px, 1, "compose left svg pixels untouched" },
    };
    check_int_cases(utest_result, untouched, sizeof_array(untouched));
}

// Raster premultiply: an opaque pixel keeps its channels in both the SIMD path (16-pixel blocks) and the scalar
// tail, and both round c * a / 255 to nearest.

UTEST(image_decode, premultiply_keeps_opaque_and_rounds_in_simd_and_scalar_paths) {
    enum { WIDTH = 20 }; // 16 pixels through the SIMD block, 4 through the scalar tail
    u8 pixels[WIDTH * 4];
    for (int i = 0; i < WIDTH; i++) {
        const bool opaque = (i % 2) == 0;
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = (u8)(100 + i);
        pixels[i * 4 + 2] = 1;
        pixels[i * 4 + 3] = opaque ? 255 : 128;
    }

    FlImageDecodeResult result = {
        .pixels = pixels,
        .width = WIDTH,
        .height = 1,
        .stride = WIDTH * 4,
        .channels = 4,
        .needs_thumbnail_compose = true,
    };
    image_thumbnail_compose(s_arena, &result, nullptr, nullptr);
    ASSERT_TRUE(result.premultiplied);

    for (int i = 0; i < WIDTH; i++) {
        const int a = pixels[i * 4 + 3];
        const int src_g = 100 + i;
        const int want_r = (255 * a + 127) / 255;
        const int want_g = (src_g * a + 127) / 255;
        const int want_b = (1 * a + 127) / 255;
        ASSERT_EQ_MSG((int)pixels[i * 4 + 0], want_r, "R is round(c * a / 255)");
        ASSERT_EQ_MSG((int)pixels[i * 4 + 1], want_g, "G is round(c * a / 255)");
        ASSERT_EQ_MSG((int)pixels[i * 4 + 2], want_b, "B is round(c * a / 255)");
        if (a == 255) {
            ASSERT_EQ_MSG((int)pixels[i * 4 + 0], 255, "an opaque channel stays 255");
        }
    }
}
