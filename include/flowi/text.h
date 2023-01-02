
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "manual.h"
#include "math_data.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FlTextApi;

struct FlText;

static void fl_text_bullet(struct FlTextApi* api, const char* text);

static void fl_text_label(struct FlTextApi* api, const char* label, const char* text);

static void fl_text_show_color(struct FlTextApi* api, FlColor color, const char* text);

static void fl_text_show(struct FlTextApi* api, const char* text);

static void fl_text_text_disabled(struct FlTextApi* api, const char* text);

#include "text.inl"

#ifdef __cplusplus
}
#endif
