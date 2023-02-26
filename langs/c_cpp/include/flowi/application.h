#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "manual.h"
#include "application_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FlContext;
struct FlIoApi;

typedef void (*FlMainLoopCallback)(struct FlContext* ctx, void* user_data);

typedef struct FlApplication {
    struct FlInternalData* priv;
    struct FlIoApi* (*io_get_api)(struct FlInternalData* data, int api_version);
    bool (*main_loop)(FlMainLoopCallback callback, void* user_data);
} FlApplication;

FlApplication* fl_application_create(FlApplicationSettings* settings);

FL_INLINE struct FlIoApi* fl_app_io_api(FlApplication* app) { return (app->io_get_api)(app->priv, 0); }

#ifdef __cplusplus
}
#endif
