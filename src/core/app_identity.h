#pragma once

#include "core.h"
#include "string.h"
#include <flowi/core/app_identity.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Application identity: app-flavored strings used by the log file and file watcher. They default to
// neutral flowi values; a host may override them at startup, before those modules first use them.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Drop every override, restoring the neutral defaults.
void app_identity_reset(void);

