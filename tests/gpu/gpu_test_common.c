#include "gpu_test_common.h"

#define AA_RAMP_MAX_POINTS 16 // comfortably above the longest run any caller walks

static bool aa_is_partial(const u8* px) {
    return px[3] > 0 && px[3] < 255;
}

static const char* aa_pixel_failure(const u8* px, u8 prev_alpha, FlColor color) {
    if (px[3] < prev_alpha) {
        return "alpha must rise monotonically along the ramp";
    }
    if (!aa_is_partial(px)) {
        return nullptr;
    }
    f32 a = (f32)px[3] / 255.0f;
    ChannelExpect premultiplied = { (f32)color.r * a, (f32)color.g * a, (f32)color.b * a, (f32)px[3] };
    if (!channels_near(px, premultiplied, 3.0f)) {
        return "an AA pixel must be the color premultiplied by its coverage";
    }
    return nullptr;
}

static const char* aa_ramp_failure(const FlPixelReadback* buf, const int (*pts)[2], int count, AaRampSpec spec) {
    u8 prev_alpha = 0;
    bool saw_partial = false;
    for (int i = 0; i < count; i++) {
        const u8* px = pixel_at(buf, pts[i][0], pts[i][1]);
        const char* failure = aa_pixel_failure(px, prev_alpha, spec.color);
        if (failure != nullptr) {
            return failure;
        }
        prev_alpha = px[3];
        saw_partial |= aa_is_partial(px);
    }
    if (prev_alpha != 255) {
        return "the ramp must end fully opaque inside the shape";
    }
    if (spec.require_partial && !saw_partial) {
        return "the edge must produce at least one partial pixel";
    }
    return nullptr;
}

// Walk a pixel run from outside a shape to inside it, asserting the SDF AA contract every edge shares.
// require_partial also demands at least one partially covered pixel, so a hard step fails.
int check_aa_ramp_line(const FlPixelReadback* buf, AaRampRun run, AaRampSpec spec) {
    int pts[AA_RAMP_MAX_POINTS][2];
    CHECK(run.count <= AA_RAMP_MAX_POINTS, "check_aa_ramp_line: run longer than the point buffer");
    for (int i = 0; i < run.count; i++) {
        pts[i][0] = run.x0 + i * run.dx;
        pts[i][1] = run.y0 + i * run.dy;
    }
    const char* failure = aa_ramp_failure(buf, pts, run.count, spec);
    if (failure != nullptr) {
        fprintf(stderr, "gpu_smoke_test FAIL: %s: %s\n", spec.what, failure);
        return 1;
    }
    return 0;
}

int check_pixel_probes(const FlPixelReadback* buf, const PixelProbe* probes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const u8* px = pixel_at(buf, probes[i].x, probes[i].y);
        bool ok = probes[i].transparent ? pixel_transparent(px) : pixel_color(px, probes[i].color, 2.0f);
        CHECK(ok, probes[i].msg);
    }
    return 0;
}

#if !defined(__APPLE__)
GpuDrawList translate_with(FlArena* out_arena, TranslateJob job) {
    return gpu_ir_to_draw_cmds(out_arena, &(GpuIrInput) { .commands = job.start,
                                                          .bytes = ir_bytes(job.ir_arena, job.start),
                                                          .global_alpha = job.global_alpha,
                                                          .target_width = TR_TARGET,
                                                          .target_height = TR_TARGET,
                                                          .atlas = job.atlas,
                                                          .slots = job.slots });
}

GpuDrawList translate(FlArena* out_arena, FlArena* ir_arena, const void* start, u8 global_alpha) {
    return translate_with(out_arena,
                          (TranslateJob) { .ir_arena = ir_arena, .start = start, .global_alpha = global_alpha });
}
#endif
