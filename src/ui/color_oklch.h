#pragma once

#include <core/types.h>
#include <flowi/core/math_data.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Perceptual color: OKLab / OKLCH
//
// OKLab is a perceptually uniform color space; OKLCH is its cylindrical form (lightness, chroma, hue).
//
// Based on Björn Ottosson's Oklab color space: https://bottosson.github.io/posts/oklab/
// Conversion constants validated against the W3C CSS Color 4 reference values.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OKLab: Cartesian perceptual color

typedef struct {
    f32 L; // Lightness: 0.0 (black) to 1.0 (white)
    f32 a; // Green-red axis: roughly -0.4 to 0.4
    f32 b; // Blue-yellow axis: roughly -0.4 to 0.4
} FlColorOKLab;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OKLCH: cylindrical perceptual color

typedef struct {
    f32 l; // Lightness: 0.0 (black) to 1.0 (white)
    f32 c; // Chroma: 0.0 (gray) to ~0.4 (saturated)
    f32 h; // Hue: 0-360 degrees
} FlColorOKLCH;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Conversions. sRGB is the 8-bit FlColor used everywhere else in the UI. OKLCH/OKLab->sRGB results are
// clamped into the displayable sRGB gamut.

FlColorOKLab color_rgb_to_oklab(FlColor rgb);
FlColor color_oklab_to_rgb(FlColorOKLab lab);
FlColorOKLCH color_rgb_to_oklch(FlColor rgb);
FlColor color_oklch_to_rgb(FlColorOKLCH oklch);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OKLab <-> OKLCH (Cartesian <-> polar). Pure math, no gamut concerns.

FlColorOKLCH color_oklab_to_oklch(FlColorOKLab lab);
FlColorOKLab color_oklch_to_oklab(FlColorOKLCH lch);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OKLCH component manipulation (perceptually uniform)

static inline FlColorOKLCH color_oklch_set_lightness(FlColorOKLCH c, f32 lightness) {
    c.l = lightness < 0.0f ? 0.0f : (lightness > 1.0f ? 1.0f : lightness);
    return c;
}

static inline FlColorOKLCH color_oklch_lighten(FlColorOKLCH c, f32 amount) {
    return color_oklch_set_lightness(c, c.l + amount);
}

static inline FlColorOKLCH color_oklch_darken(FlColorOKLCH c, f32 amount) {
    return color_oklch_set_lightness(c, c.l - amount);
}

static inline FlColorOKLCH color_oklch_set_chroma(FlColorOKLCH c, f32 chroma) {
    c.c = chroma < 0.0f ? 0.0f : chroma;
    return c;
}

// Desaturate by a fraction of current chroma: amount 0 leaves it unchanged, 1 makes it fully gray.
static inline FlColorOKLCH color_oklch_desaturate(FlColorOKLCH c, f32 amount) {
    f32 t = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
    return color_oklch_set_chroma(c, c.c * (1.0f - t));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Perceptual derivation over sRGB. Each round-trips through OKLCH and preserves the input alpha.

FlColor color_lighten_perceptual(FlColor base, f32 amount);
FlColor color_darken_perceptual(FlColor base, f32 amount);
FlColor color_desaturate_perceptual(FlColor base, f32 amount);

// Perceptual lerp between two sRGB colors through OKLab. Cartesian (L, a, b) so it stays
// well-defined through gray, where hue is undefined - unlike an OKLCH hue lerp. Alpha is
// lerped linearly in sRGB (OKLab carries no alpha).
FlColor color_lerp_oklab(FlColor a, FlColor b, f32 t);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Luminance / contrast. Lightness is the OKLCH L of the color (perceptual, 0..1).

f32 color_perceptual_lightness(FlColor color);

// Pick whichever of light/dark reads better on top of bg, by OKLCH lightness of the background.
FlColor color_readable_foreground(FlColor bg, FlColor light, FlColor dark);
