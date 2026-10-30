#pragma once

#include <core/types.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reference-fit scaling policy (opt-in battery)
//
// The fullscreen/TV/console scale-to-fit policy: UI is authored at a 1080p reference and
// scaled down at lower resolutions (PAL/SD/720p), with an extra text boost to keep type
// readable. A bigger display shows the same UI bigger, not more content.
//
//   ui_scale = actual_height / 1080.0
//   text_scale = ui_scale * text_scale_factor

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct UiScalePreset {
    int min_height;          // inclusive
    int max_height;          // inclusive
    float text_scale_factor; // 1.0 = no boost
} UiScalePreset;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Height-based presets: text gets a boost at lower resolutions to maintain readability.

// clang-format off
static const UiScalePreset UI_SCALE_PRESETS[] = {
    // 480p/576p
    { 0, 600, 1.10f },

    // 720p
    { 601, 900, 1.05f },

    // 1080p+
    { 901, 9999, 1.0f },
};
// clang-format on

#define UI_SCALE_PRESETS_COUNT (sizeof(UI_SCALE_PRESETS) / sizeof(UI_SCALE_PRESETS[0]))

// Reference resolution (all UI code assumes this resolution)
#define UI_REFERENCE_HEIGHT 1080.0f

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A pair of absolute scales ready to feed straight into ui_set_scale

typedef struct UiScalePair {
    float ui_scale;
    float text_scale;
} UiScalePair;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Apply the scale preset matching a display height in pixels.

static inline void ui_apply_scale_preset(float* out_ui_scale, float* out_text_scale_factor, float* out_text_scale,
                                         int height) {
    *out_ui_scale = (float)height / UI_REFERENCE_HEIGHT;

    *out_text_scale_factor = 1.0f;
    for (u32 i = 0; i < UI_SCALE_PRESETS_COUNT; i++) {
        if (height >= UI_SCALE_PRESETS[i].min_height && height <= UI_SCALE_PRESETS[i].max_height) {
            *out_text_scale_factor = UI_SCALE_PRESETS[i].text_scale_factor;
            break;
        }
    }

    *out_text_scale = (*out_ui_scale) * (*out_text_scale_factor);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The two absolute scales for a given display height, ready to push into ui_set_scale.

static inline UiScalePair ui_reference_fit(int height) {
    UiScalePair pair;
    float text_scale_factor;
    ui_apply_scale_preset(&pair.ui_scale, &text_scale_factor, &pair.text_scale, height);
    return pair;
}
