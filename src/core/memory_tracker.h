#pragma once

// Internal include for the memory tracker, layered over the generated <flowi/core/memory_tracker.h>,
// which names FlString by value and so must be included after <core/string.h>.

#include <core/string.h>
#include <flowi/core/memory_tracker.h>
