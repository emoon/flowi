// color, background, software command and image-convert tests

#include "render/common.h"

// The perceptual color module (ui/color_oklch.c), which needs no renderer: sRGB <-> OKLab <-> OKLCH conversions,
// out-of-gamut clamping, perceptual lighten, darken and desaturate, and the luminance-based choice of a readable
// foreground.

// Reference OKLab and OKLCH values for the sRGB primaries, from CSS Color 4 and Ottosson's reference.
UTEST(color_oklch, known_values_primaries) {
    const FlColorOKLab white = color_rgb_to_oklab((FlColor) { 255, 255, 255, 255 });
    const FlColorOKLab red = color_rgb_to_oklab((FlColor) { 255, 0, 0, 255 });
    const FlColorOKLCH red_lch = color_rgb_to_oklch((FlColor) { 255, 0, 0, 255 });
    const FlColorOKLCH green_lch = color_rgb_to_oklch((FlColor) { 0, 255, 0, 255 });
    const FlColorOKLCH blue_lch = color_rgb_to_oklch((FlColor) { 0, 0, 255, 255 });

    const NearCase oklab[] = {
        { white.L, 1.0f, 0.005f, "white L" },
        { white.a, 0.0f, 0.005f, "white a" },
        { white.b, 0.0f, 0.005f, "white b" },
        { color_rgb_to_oklab((FlColor) { 0, 0, 0, 255 }).L, 0.0f, 0.005f, "black L" },
        { red.L, 0.6279554f, 0.005f, "red L" },
        { red.a, 0.2248631f, 0.005f, "red a" },
        { red.b, 0.1258463f, 0.005f, "red b" },
    };
    expect_near_cases(utest_result, oklab, sizeof_array(oklab));

    const NearCase oklch[] = {
        { red_lch.l, 0.6279554f, 0.005f, "red lightness" },
        { red_lch.c, 0.2576833f, 0.005f, "red chroma" },
        { red_lch.h, 29.234f, 0.5f, "red hue" },
        { green_lch.h, 142.495f, 0.5f, "green hue" },
        { blue_lch.h, 264.052f, 0.5f, "blue hue" },
    };
    expect_near_cases(utest_result, oklch, sizeof_array(oklch));
}

UTEST(color_oklch, roundtrip_srgb) {
    const FlColor samples[] = {
        { 18, 52, 86, 255 }, { 200, 120, 40, 255 },  { 12, 200, 175, 255 }, { 255, 0, 0, 255 },
        { 0, 128, 64, 255 }, { 130, 130, 130, 255 }, { 0, 0, 0, 255 },      { 255, 255, 255, 255 },
    };
    for (int i = 0; i < (int)sizeof_array(samples); i++) {
        FlColor in = samples[i];
        expect_rgb_near(utest_result, color_oklch_to_rgb(color_rgb_to_oklch(in)), in, 2.0f);
        expect_rgb_near(utest_result, color_oklab_to_rgb(color_rgb_to_oklab(in)), in, 2.0f);
    }
}

// Chroma far past the sRGB gamut comes back clamped and does not wrap.
UTEST(color_oklch, out_of_gamut_clamped) {
    FlColorOKLCH wild = { .l = 0.7f, .c = 0.5f, .h = 29.0f }; // c=0.5 is well outside sRGB for this L/h
    FlColor rgb = color_oklch_to_rgb(wild);
    EXPECT_LE((int)rgb.r, 255);
    EXPECT_GE((int)rgb.r, 0);

    FlColorOKLCH back = color_rgb_to_oklch(rgb);
    EXPECT_LE(back.c, wild.c + 0.001f);
    EXPECT_GT(back.c, 0.0f);
}

UTEST(color_oklch, perceptual_lighten_darken_monotonic) {
    FlColor base = { 90, 110, 160, 255 };
    f32 l0 = color_perceptual_lightness(base);
    f32 l_up = color_perceptual_lightness(color_lighten_perceptual(base, 0.15f));
    f32 l_dn = color_perceptual_lightness(color_darken_perceptual(base, 0.15f));
    EXPECT_GT(l_up, l0);
    EXPECT_LT(l_dn, l0);

    // Alpha is preserved through derivation.
    FlColor with_alpha = { 90, 110, 160, 128 };
    EXPECT_EQ((int)color_lighten_perceptual(with_alpha, 0.1f).a, 128);
    EXPECT_EQ((int)color_darken_perceptual(with_alpha, 0.1f).a, 128);
}

UTEST(color_oklch, lerp_oklab_endpoints_and_alpha) {
    FlColor a = { 200, 30, 40, 255 };
    FlColor b = { 255, 255, 255, 255 };

    EXPECT_NEAR(color_perceptual_lightness(color_lerp_oklab(a, b, 0.0f)), color_perceptual_lightness(a), 0.01f);
    EXPECT_NEAR(color_perceptual_lightness(color_lerp_oklab(a, b, 1.0f)), color_perceptual_lightness(b), 0.01f);

    // The midpoint's lightness is strictly between the endpoints', since OKLab L is monotone.
    f32 lm = color_perceptual_lightness(color_lerp_oklab(a, b, 0.5f));
    EXPECT_GT(lm, color_perceptual_lightness(a));
    EXPECT_LT(lm, color_perceptual_lightness(b));

    // Alpha is interpolated linearly in sRGB, independently of the OKLab path.
    FlColor opaque = { 0, 0, 0, 0 };
    FlColor solid = { 0, 0, 0, 255 };
    const IntCase alphas[] = {
        { (int)color_lerp_oklab(opaque, solid, 0.0f).a, 0, "alpha at t=0" },
        { (int)color_lerp_oklab(opaque, solid, 1.0f).a, 255, "alpha at t=1" },
        { (int)color_lerp_oklab(opaque, solid, 0.5f).a, 127, "alpha at t=0.5" },
    };
    expect_int_cases(utest_result, alphas, sizeof_array(alphas));
}

UTEST(color_oklch, desaturate_reduces_chroma) {
    FlColor base = { 200, 40, 40, 255 };
    FlColorOKLCH c0 = color_rgb_to_oklch(base);
    FlColorOKLCH c_half = color_oklch_desaturate(c0, 0.5f);
    EXPECT_NEAR(c_half.c, c0.c * 0.5f, 0.0001f);
    EXPECT_LT(c_half.c, c0.c);

    FlColor desat = color_desaturate_perceptual((FlColor) { 200, 40, 40, 77 }, 0.6f);
    EXPECT_LT(color_rgb_to_oklch(desat).c, c0.c);
    EXPECT_EQ((int)desat.a, 77);
}

UTEST(color_oklch, readable_foreground_picks_by_lightness) {
    FlColor light = { 245, 245, 245, 255 };
    FlColor dark = { 20, 20, 20, 255 };

    // Saturated cases: bright yellow gets a dark foreground and deep blue a light one.
    const IntCase picks[] = {
        { (int)color_readable_foreground((FlColor) { 255, 255, 255, 255 }, light, dark).r, (int)dark.r,
          "white -> dark" },
        { (int)color_readable_foreground((FlColor) { 0, 0, 0, 255 }, light, dark).r, (int)light.r, "black -> light" },
        { (int)color_readable_foreground((FlColor) { 255, 220, 0, 255 }, light, dark).r, (int)dark.r,
          "yellow -> dark" },
        { (int)color_readable_foreground((FlColor) { 20, 20, 140, 255 }, light, dark).r, (int)light.r,
          "blue -> light" },
    };
    expect_int_cases(utest_result, picks, sizeof_array(picks));
}

// The background controller's state and the FlBackground snapshot (ui/background.{c,h}).

// The invalid ImageHandle is 0, so any nonzero value stands in for a real VFS handle.
#define H_A ((ImageHandle)7)

#define H_B ((ImageHandle)42)

UTEST(background, defaults) {
    FlBackgroundController c;
    background_controller_init(&c);

    const BoolCase defaults[] = {
        { c.state.current == IMAGE_HANDLE_INVALID, true, "init: current should be invalid" },
        { c.state.prev == IMAGE_HANDLE_INVALID, true, "init: prev should be invalid (no blend)" },
        { c.state.alpha == 255, true, "init: alpha should be opaque" },
        { c.state.clear_r == 14 && c.state.clear_g == 14 && c.state.clear_b == 16, true, "init: clear color default" },
    };
    check_bool_cases(utest_result, defaults, sizeof_array(defaults));
}

UTEST(background, set_alpha_clear_color) {
    FlBackgroundController c;
    background_controller_init(&c);

    background_controller_set(&c, H_A);
    ASSERT_TRUE_MSG(c.state.current == H_A, "set: current updated");
    background_controller_set_alpha(&c, 128);
    ASSERT_TRUE_MSG(c.state.alpha == 128, "set_alpha: alpha updated");
    background_controller_set_clear_color(&c, 1, 2, 3);
    ASSERT_TRUE_MSG(c.state.clear_r == 1 && c.state.clear_g == 2 && c.state.clear_b == 3, "set_clear_color: updated");

    // set(valid) always resets alpha to opaque.
    background_controller_set(&c, H_B);
    ASSERT_TRUE_MSG(c.state.current == H_B, "set(valid): current updated");
    ASSERT_TRUE_MSG(c.state.alpha == 255, "set(valid): alpha reset to opaque");

    // set(INVALID) clears the image and resets alpha to opaque.
    background_controller_set_alpha(&c, 42);
    background_controller_set(&c, IMAGE_HANDLE_INVALID);
    ASSERT_TRUE_MSG(c.state.current == IMAGE_HANDLE_INVALID, "set(invalid): current cleared");
    ASSERT_TRUE_MSG(c.state.alpha == 255, "set(invalid): alpha reset to opaque");
}

// set_blend keeps no state beyond prev, current and t. A plain set() drops the blend by clearing prev, which is
// how an app finishes a crossfade: set(next).
UTEST(background, set_blend) {
    FlBackgroundController c;
    background_controller_init(&c);

    background_controller_set_blend(&c, H_A, H_B, 128);
    ASSERT_TRUE_MSG(c.state.prev == H_A, "set_blend: prev stored");
    ASSERT_TRUE_MSG(c.state.current == H_B, "set_blend: current stored");
    ASSERT_TRUE_MSG(c.state.t == 128, "set_blend: t stored");

    background_controller_set(&c, H_B);
    ASSERT_TRUE_MSG(c.state.prev == IMAGE_HANDLE_INVALID, "set after blend: prev cleared");
    ASSERT_TRUE_MSG(c.state.current == H_B, "set after blend: current retained");
}

UTEST(background, zero_initialised_handles_are_invalid) {
    const FlListItem item = { 0 };
    const FlBackground bg = { 0 };
    ASSERT_EQ_MSG(item.icon, FL_IMAGE_HANDLE_INVALID, "a zeroed list item has no icon");
    ASSERT_EQ_MSG(bg.current, FL_IMAGE_HANDLE_INVALID, "a zeroed background has no image");
    ASSERT_EQ_MSG(bg.prev, FL_IMAGE_HANDLE_INVALID, "a zeroed background has no crossfade");
}

UTEST(background, snapshot_handle_translation) {
    FlBackgroundController c;
    background_controller_init(&c);
    FlBackground snap = background_controller_snapshot(&c);
    const BoolCase initial[] = {
        { snap.current == FL_IMAGE_HANDLE_INVALID, true, "snapshot: no image -> invalid handle" },
        { snap.prev == FL_IMAGE_HANDLE_INVALID, true, "snapshot: no crossfade -> prev invalid" },
        { snap.alpha == 255, true, "snapshot: alpha carried" },
        { snap.clear.r == 14 && snap.clear.g == 14 && snap.clear.b == 16 && snap.clear.a == 255, true,
          "snapshot: clear color carried (opaque)" },
    };
    check_bool_cases(utest_result, initial, sizeof_array(initial));

    background_controller_set(&c, H_A);
    background_controller_set_alpha(&c, 77);
    snap = background_controller_snapshot(&c);
    const BoolCase single[] = {
        { snap.current == (FlImageHandle)H_A, true, "snapshot: valid handle passes through numerically" },
        { snap.prev == FL_IMAGE_HANDLE_INVALID, true, "snapshot: prev invalid without a blend (single image)" },
        { snap.alpha == 77, true, "snapshot: alpha value carried" },
    };
    check_bool_cases(utest_result, single, sizeof_array(single));

    // A blend carries both handles, and t reaches the backend normalized to 0..1 (0 = all prev, 1 = all current).
    background_controller_set_blend(&c, H_A, H_B, 0);
    snap = background_controller_snapshot(&c);
    const BoolCase blend[] = {
        { snap.prev == (FlImageHandle)H_A, true, "snapshot: blend prev carried" },
        { snap.current == (FlImageHandle)H_B, true, "snapshot: blend current carried" },
        { snap.crossfade_t == 0.0f, true, "snapshot: t=0 -> full prev" },
    };
    check_bool_cases(utest_result, blend, sizeof_array(blend));

    background_controller_set_blend(&c, H_A, H_B, 255);
    snap = background_controller_snapshot(&c);
    ASSERT_TRUE_MSG(snap.crossfade_t == 1.0f, "snapshot: t=255 -> full current");

    // set_blend with an invalid prev falls back to the single-image path: no blend reaches the backend, and the
    // current image uses its alpha. set_blend does not touch alpha, so the earlier value is kept.
    background_controller_set_alpha(&c, 90);
    background_controller_set_blend(&c, IMAGE_HANDLE_INVALID, H_B, 128);
    snap = background_controller_snapshot(&c);
    const BoolCase fallback[] = {
        { snap.prev == FL_IMAGE_HANDLE_INVALID, true, "snapshot: invalid prev -> single image, no blend" },
        { snap.current == (FlImageHandle)H_B, true, "snapshot: single-image current carried" },
        { snap.alpha == 90, true, "snapshot: single image rides its alpha" },
    };
    check_bool_cases(utest_result, fallback, sizeof_array(fallback));
}

UTEST(background, blend_t_round_trip) {
    // The snapshot normalizes the u8 blend to 0..1, and (t*255 + 0.5) recovers every value 0..255 exactly.
    FlBackgroundController c;
    background_controller_init(&c);
    for (int t = 0; t <= 255; t++) {
        background_controller_set_blend(&c, H_A, H_B, (u8)t);
        FlBackground s = background_controller_snapshot(&c);
        u8 reconstructed = (u8)(s.crossfade_t * 255.0f + 0.5f);
        ASSERT_TRUE_MSG(reconstructed == (u8)t, "blend t round-trip not lossless");
    }
}

// sr_command_split_rounded: the general 9-grid case emits a corner command only for a non-zero corner, so the
// arena advance, which the caller reads through arena_count, equals corners written + 5. Every written command is
// a copy of initial_command, so a valid slot always has the base mask bit. An uninitialized slot on a new arena
// reads back as a zero mask.
UTEST(sr_command, split_rounded_count_matches_written_commands) {
    struct {
        float tl, tr, bl, br;
        int expected; // commands the caller sees. 0 corners never reaches the general case
        int corners;  // corner-flagged commands expected among them
        const char* what;
    } cases[] = {
        { 8, 8, 8, 0, 8, 3, "br zero" },      // one zero corner, in each position
        { 8, 8, 0, 8, 8, 3, "bl zero" },      //
        { 8, 0, 8, 8, 8, 3, "tr zero" },      //
        { 0, 8, 8, 8, 8, 3, "tl zero" },      //
        { 8, 0, 0, 8, 7, 2, "diagonal" },     // two zero corners, which is the general case and not an edge case
        { 8, 6, 4, 2, 9, 4, "all positive" }, // non-uniform with no zeros: all 9 commands
        { 8, 8, 8, 8, 7, 4, "uniform" },      // the uniform fast path, 7 commands
        { 0, 0, 0, 0, 1, 0, "no rounding" },  // a single passthrough command
    };

    for (size_t i = 0; i < sizeof_array(cases); ++i) {
        FlArena* arena = arena_new();
        fl_arena_align(arena, 16);
        RenderCommand* start = (RenderCommand*)fl_arena_current_pos(arena);

        RenderCommand initial = { 0 };
        initial.mask = RenderMask_DrawQuad;
        initial.aabb = f32x4_new(0.0f, 0.0f, 100.0f, 100.0f);

        f32x4 corners = f32x4_new(cases[i].tl, cases[i].tr, cases[i].bl, cases[i].br);
        sr_command_split_rounded(arena, &initial, corners);

        int count = arena_count(arena, start, RenderCommand);
        ASSERT_EQ_MSG(count, cases[i].expected, cases[i].what);

        int corner_cmds = 0;
        u32 seen_corners = 0;
        for (int c = 0; c < count; ++c) {
            // A slot the split wrote is a copy of initial_command, so it has the base mask bit. A slot left
            // uninitialized reads back as zero on a new arena.
            ASSERT_TRUE_MSG((start[c].mask & RenderMask_DrawQuad) != 0, cases[i].what);
            if (start[c].mask & RenderMask_Corners) {
                corner_cmds++;
                // The selector is a value in the corners field and not a bitmask. A split names each corner
                // at most once, and every corner command names exactly one.
                RenderCorner corner = render_mask_corner_of(start[c].mask);
                ASSERT_TRUE_MSG(corner >= RenderCorner_TopLeft && corner <= RenderCorner_BottomRight, cases[i].what);
                ASSERT_TRUE_MSG((seen_corners & (1u << corner)) == 0, cases[i].what);
                seen_corners |= 1u << corner;
            }
        }
        ASSERT_EQ_MSG(corner_cmds, cases[i].corners, cases[i].what);

        fl_arena_destroy(arena);
    }
}

// The plane must hold base + i at index i.
static void check_ramp_plane(int* utest_result, const u8* plane, int count, int base) {
    for (int i = 0; i < count; i++) {
        ASSERT_EQ((int)plane[i], i + base);
    }
}

// The plane must hold value at every index.
static void check_filled_plane(int* utest_result, const u8* plane, int count, int value) {
    for (int i = 0; i < count; i++) {
        ASSERT_EQ((int)plane[i], value);
    }
}

// The render cases cover the 3-channel deinterleave and the planar paths with keep_alpha=false. These cover the
// rest: the alpha plane with keep_alpha=true and the XRGB format.
UTEST(sr_image_convert, deinterleave_pixels_all_branches) {
    // 20 pixels is one 16-wide SIMD block plus a 4-pixel scalar tail, so both code paths run.
    enum { COUNT = 20 };
    u8 src4[COUNT * 4];
    u8 src3[COUNT * 3];
    for (int i = 0; i < COUNT; i++) {
        src4[i * 4 + 0] = (u8)(i + 1);   // R
        src4[i * 4 + 1] = (u8)(i + 50);  // G
        src4[i * 4 + 2] = (u8)(i + 100); // B
        src4[i * 4 + 3] = (u8)(i + 150); // A
        src3[i * 3 + 0] = (u8)(i + 2);
        src3[i * 3 + 1] = (u8)(i + 60);
        src3[i * 3 + 2] = (u8)(i + 120);
    }

    u8 r[COUNT], g[COUNT], b[COUNT], a[COUNT];

    // 4 channels, keep_alpha=true: all four planes are written.
    memset(a, 0xEE, sizeof a);
    sr_deinterleave_pixels(src4, COUNT, 4, &(PlanarPlanes) { .r = r, .g = g, .b = b, .a = a, .keep_alpha = true });
    check_ramp_plane(utest_result, r, COUNT, 1);
    check_ramp_plane(utest_result, g, COUNT, 50);
    check_ramp_plane(utest_result, b, COUNT, 100);
    check_ramp_plane(utest_result, a, COUNT, 150);

    // 4 channels, keep_alpha=false: RGB is written and the alpha buffer is left untouched.
    memset(a, 0xEE, sizeof a);
    sr_deinterleave_pixels(src4, COUNT, 4, &(PlanarPlanes) { .r = r, .g = g, .b = b, .a = a, .keep_alpha = false });
    check_ramp_plane(utest_result, r, COUNT, 1);
    check_ramp_plane(utest_result, g, COUNT, 50);
    check_ramp_plane(utest_result, b, COUNT, 100);
    check_filled_plane(utest_result, a, COUNT, 0xEE);

    // A 3-channel source takes the RGB deinterleave path.
    sr_deinterleave_pixels(src3, COUNT, 3,
                           &(PlanarPlanes) { .r = r, .g = g, .b = b, .a = nullptr, .keep_alpha = false });
    check_ramp_plane(utest_result, r, COUNT, 2);
    check_ramp_plane(utest_result, g, COUNT, 60);
    check_ramp_plane(utest_result, b, COUNT, 120);
}

UTEST(sr_image_convert, convert_to_planar_xrgb_and_alpha) {
    FlArena* arena = arena_new();

    // A 3x2 interleaved source. Pixel (x,y) has different bytes per channel, to catch channel swaps.
    const int w = 3, h = 2;
    u8 pixels[3 * 2 * 4];
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            u8* p = pixels + (y * w + x) * 4;
            p[0] = (u8)(y * 10 + x + 1);  // R
            p[1] = (u8)(y * 10 + x + 31); // G
            p[2] = (u8)(y * 10 + x + 61); // B
            p[3] = (u8)(y * 10 + x + 91); // A / unused X
        }
    }

    ImageData src = {
        .data = pixels,
        .width = w,
        .height = h,
        .padded_width = w,
        .padded_height = h,
        .stride = w * 4,
        .channels = 4,
        .format = FlImageFormat_XRGB,
    };

    // XRGB drops the X byte (keep_alpha=false), giving 3 planes with bilinear padding (+1 in w and h).
    ImageData* xrgb = sr_convert_to_planar(&src, false, arena);
    ASSERT_TRUE(xrgb != nullptr);
    const IntCase layout[] = {
        { xrgb->channels, 3, "XRGB drops the X byte" },
        { (int)xrgb->format, (int)FlImageFormat_Deinterleaved, "the result is planar" },
        { xrgb->padded_width, w + 1, "one padding column" },
        { xrgb->padded_height, h + 1, "one padding row" },
    };
    check_int_cases(utest_result, layout, sizeof_array(layout));

    int plane = xrgb->padded_width * xrgb->padded_height;
    const u8* rp = xrgb->data;
    const u8* gp = xrgb->data + plane;
    const u8* bp = xrgb->data + plane * 2;
    int s = xrgb->stride; // == padded_width

    // Logical pixels (0,0) and (2,1) are deinterleaved correctly. The padding column duplicates the last column on
    // row 0, and the padding row duplicates the last row.
    const IntCase pixels_out[] = {
        { (int)rp[0], 1, "R of (0,0)" },
        { (int)gp[0], 31, "G of (0,0)" },
        { (int)bp[0], 61, "B of (0,0)" },
        { (int)rp[1 * s + 2], 13, "R of (2,1) = 1*10+2+1" },
        { (int)rp[w], (int)rp[w - 1], "the padding column duplicates the last column" },
        { (int)rp[h * s], (int)rp[(h - 1) * s], "the padding row duplicates the last row" },
    };
    check_int_cases(utest_result, pixels_out, sizeof_array(pixels_out));

    // The same source as RGBA with keep_alpha=true gives 4 planes, including alpha.
    src.format = FlImageFormat_RGBA;
    ImageData* rgba = sr_convert_to_planar(&src, true, arena);
    ASSERT_TRUE(rgba != nullptr);
    ASSERT_EQ(rgba->channels, 4);
    const u8* ap = rgba->data + (rgba->padded_width * rgba->padded_height) * 3;
    ASSERT_EQ((int)ap[0], 91); // A of (0,0)

    fl_arena_destroy(arena);
}

// A zero extent has no last pixel or row to duplicate, so the edge padding would read before the plane. The
// conversion rejects it.

UTEST(sr_image_convert, convert_to_planar_rejects_a_zero_extent) {
    FlArena* arena = arena_new();

    u8 pixel[4] = { 1, 2, 3, 4 };
    ImageData src = {
        .data = pixel,
        .width = 1,
        .height = 1,
        .padded_width = 1,
        .padded_height = 1,
        .stride = 4,
        .channels = 4,
        .format = FlImageFormat_RGBA,
    };

    ASSERT_TRUE_MSG(sr_convert_to_planar(&src, false, arena) != nullptr, "a 1x1 source still converts");

    src.width = 0;
    ASSERT_TRUE_MSG(sr_convert_to_planar(&src, false, arena) == nullptr, "a zero width is rejected");

    src.width = 1;
    src.height = 0;
    ASSERT_TRUE_MSG(sr_convert_to_planar(&src, false, arena) == nullptr, "a zero height is rejected");

    fl_arena_destroy(arena);
}

static float emitted_glyph_x(float pen_x) {
    static u8 coverage[4] = { 255, 255, 255, 255 };
    const Glyph glyph = { .bitmap_data_r = coverage, .advance_x = 2 * 64, .pixel_width = 2, .width = 2, .height = 2 };
    fl_arena_align(s_arena, _Alignof(RenderCommand));
    const RenderCommand* cmd = arena_pos_type(s_arena, RenderCommand);
    sr_emit_glyph(s_arena, &glyph, (GlyphPen) { pen_x, 20.0f }, (GlyphStyle) { (u8x16) { 0 }, 255 });
    return f32x4_extract(cmd->render_text.aabb, 0);
}

UTEST(software_text, glyph_pen_x_rounds_to_the_nearest_pixel) {
    const NearCase pens[] = {
        { emitted_glyph_x(10.0f), 10.0f, 0.0, "a whole pixel pen stays put" },
        { emitted_glyph_x(10.4f), 10.0f, 0.0, "below half a pixel rounds down" },
        { emitted_glyph_x(10.5f), 11.0f, 0.0, "half a pixel rounds up" },
        { emitted_glyph_x(10.6f), 11.0f, 0.0, "above half a pixel rounds up" },
        { emitted_glyph_x(-0.4f), 0.0f, 0.0, "a pen just left of zero rounds to zero" },
    };
    check_near_cases(utest_result, pens, sizeof_array(pens));
}
