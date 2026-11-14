#pragma once

#include <core/types.h>

typedef struct FlUiCtx FlUiCtx;

typedef struct WheelScope {
    u32 parent_id;
    bool scrolls;
} WheelScope;

void wheel_scope_begin(FlUiCtx* ctx, u32 element_id, bool scrolls);

void wheel_scope_end(FlUiCtx* ctx);

bool wheel_scope_hovered(const FlUiCtx* ctx, u32 element_id);

bool wheel_scope_owns_wheel(const FlUiCtx* ctx, u32 element_id);
