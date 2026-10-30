#pragma once

#include <core/core.h>
#include <core/string.h>
#include <ui/ui_types.h>
// FlCursor / FlCursorShape are authored in cursor.def; api_gen owns them.
#include <flowi/ui/cursor.h>

// The fl_input_* event types, their discriminant enums, and the fl_input_* push /
// update / state-init API are authored in api_gen's input.def and generated into the
// public header below. This file is the internal include seam platform/ui code
// reaches that generated surface - and its transitive deps - through.
#include <flowi/platform/input.h>
