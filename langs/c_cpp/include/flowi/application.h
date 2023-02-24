
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

struct FlApplicationApi;

struct FlApplication;

typedef void (*FlMainLoopCallback)(struct FlContext* ctx, void* user_data);

static struct FlContext* fl_application_create(const char* application_name, const char* developer);

static bool fl_application_main_loop(FlMainLoopCallback callback, void* user_data);

#include "application.inl"

#ifdef __cplusplus
}
#endif