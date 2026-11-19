#pragma once

// Shared fixtures for the unit_tests_localization*.c translation units: the example-data mount, the frame-boundary
// settle loop, the fixture lifecycle and the one-frame text emitter.

#include "widgets/common.h"
#include "ui/localization.h"
#include <flowi/ui/localization.h>

FlVfsMount* localization_mount(void);

// Runs frame boundaries until nothing is in flight, as an app tick loop does. Every read runs on a VFS worker, so
// a case that wants a loaded table has to tick until it arrives.
void localization_settle(struct Localization* loc);

// Initializes localization against the example data and ticks until the default table is loaded.
FlUiCtx* localization_fixture(FlVfsMount** out_mount, FlString manifest_path);

void localization_fixture_release(FlUiCtx* ctx, FlVfsMount* mount);

// Emits one frame holding label as text inside an element identified by id_source, then serializes the tree.
// Returns the size the snapshot needed, so a caller can tell a truncated buffer from an absent string.
typedef struct SnapshotBuffer {
    char* data;
    int32_t size;
} SnapshotBuffer;

#define SNAPSHOT_BUFFER(array) ((SnapshotBuffer) { (array), (int32_t)sizeof(array) })

int32_t localization_emit(FlUiCtx* ctx, FlString id_source, FlString label, SnapshotBuffer out);
