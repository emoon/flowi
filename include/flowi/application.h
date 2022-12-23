
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "manual.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FlApplication;
typedef void (*FlMainLoopCallback)(struct FlContext* ctx, void* user_data);

// TODO: More options
static struct FlContext* fl_application_create(const char* application_name, const char* developer);

static void fl_application_main_loop(FlMainLoopCallback callback, void* userdata);

#include "application.inl"

#ifdef __cplusplus
}
#endif