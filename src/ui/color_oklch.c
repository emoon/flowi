#include "color_oklch.h"
#include <math.h>

#define PI_F 3.14159265358979323846f

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OKLab conversion, after Björn Ottosson's reference implementation:
// https://bottosson.github.io/posts/oklab/
//
// Path: sRGB <-> linear RGB <-> OKLab <-> OKLCH. Matrix constants are from the Oklab paper.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f32 srgb_to_linear(f32 c) {
    if (c <= 0.04045f)
        return c / 12.92f;
    return powf((c + 0.055f) / 1.055f, 2.4f);
}

static inline f32 linear_to_srgb(f32 c) {
    if (c <= 0.0031308f)
        return 12.92f * c;
    return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

static inline f32 clamp01(f32 x) {
    if (x < 0.0f)
        return 0.0f;
    if (x > 1.0f)
        return 1.0f;
    return x;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// linear sRGB <-> OKLab

static FlColorOKLab linear_srgb_to_oklab(f32 r, f32 g, f32 b) {
    f32 l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    f32 m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    f32 s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;

    f32 l_ = cbrtf(l);
    f32 m_ = cbrtf(m);
    f32 s_ = cbrtf(s);

    return (FlColorOKLab) {
        .L = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        .a = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        .b = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_,
    };
}

static void oklab_to_linear_srgb(FlColorOKLab lab, f32* r, f32* g, f32* b) {
    f32 l_ = lab.L + 0.3963377774f * lab.a + 0.2158037573f * lab.b;
    f32 m_ = lab.L - 0.1055613458f * lab.a - 0.0638541728f * lab.b;
    f32 s_ = lab.L - 0.0894841775f * lab.a - 1.2914855480f * lab.b;

    f32 l = l_ * l_ * l_;
    f32 m = m_ * m_ * m_;
    f32 s = s_ * s_ * s_;

    *r = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    *g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    *b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// sRGB (0-255) <-> OKLab

FlColorOKLab color_rgb_to_oklab(FlColor rgb) {
    f32 r = srgb_to_linear(rgb.r / 255.0f);
    f32 g = srgb_to_linear(rgb.g / 255.0f);
    f32 b = srgb_to_linear(rgb.b / 255.0f);
    return linear_srgb_to_oklab(r, g, b);
}

FlColor color_oklab_to_rgb(FlColorOKLab lab) {
    f32 r, g, b;
    oklab_to_linear_srgb(lab, &r, &g, &b);

    // Per-channel clamp into the displayable sRGB gamut so out-of-gamut OKLCH never wraps the u8 cast.
    r = linear_to_srgb(clamp01(r));
    g = linear_to_srgb(clamp01(g));
    b = linear_to_srgb(clamp01(b));

    return (FlColor) {
        .r = (u8)(r * 255.0f + 0.5f),
        .g = (u8)(g * 255.0f + 0.5f),
        .b = (u8)(b * 255.0f + 0.5f),
        .a = 255,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlColorOKLCH color_oklab_to_oklch(FlColorOKLab lab) {
    f32 c = sqrtf(lab.a * lab.a + lab.b * lab.b);
    f32 h = atan2f(lab.b, lab.a) * (180.0f / PI_F);
    if (h < 0.0f)
        h += 360.0f;
    return (FlColorOKLCH) { .l = lab.L, .c = c, .h = h };
}

FlColorOKLab color_oklch_to_oklab(FlColorOKLCH lch) {
    f32 h_rad = lch.h * (PI_F / 180.0f);
    return (FlColorOKLab) {
        .L = lch.l,
        .a = lch.c * cosf(h_rad),
        .b = lch.c * sinf(h_rad),
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// sRGB (0-255) <-> OKLCH

FlColorOKLCH color_rgb_to_oklch(FlColor rgb) {
    return color_oklab_to_oklch(color_rgb_to_oklab(rgb));
}

FlColor color_oklch_to_rgb(FlColorOKLCH oklch) {
    return color_oklab_to_rgb(color_oklch_to_oklab(oklch));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlColor color_lighten_perceptual(FlColor base, f32 amount) {
    FlColor out = color_oklch_to_rgb(color_oklch_lighten(color_rgb_to_oklch(base), amount));
    out.a = base.a;
    return out;
}

FlColor color_darken_perceptual(FlColor base, f32 amount) {
    FlColor out = color_oklch_to_rgb(color_oklch_darken(color_rgb_to_oklch(base), amount));
    out.a = base.a;
    return out;
}

FlColor color_desaturate_perceptual(FlColor base, f32 amount) {
    FlColor out = color_oklch_to_rgb(color_oklch_desaturate(color_rgb_to_oklch(base), amount));
    out.a = base.a;
    return out;
}

FlColor color_lerp_oklab(FlColor a, FlColor b, f32 t) {
    FlColorOKLab la = color_rgb_to_oklab(a);
    FlColorOKLab lb = color_rgb_to_oklab(b);
    FlColorOKLab lm = {
        .L = la.L + (lb.L - la.L) * t,
        .a = la.a + (lb.a - la.a) * t,
        .b = la.b + (lb.b - la.b) * t,
    };
    FlColor out = color_oklab_to_rgb(lm);
    out.a = (u8)(a.a + (b.a - a.a) * t);
    return out;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

f32 color_perceptual_lightness(FlColor color) {
    return color_rgb_to_oklab(color).L;
}

FlColor color_readable_foreground(FlColor bg, FlColor light, FlColor dark) {
    // OKLab L is perceptual lightness; its midpoint is the light/dark boundary.
    return color_perceptual_lightness(bg) < 0.5f ? light : dark;
}
