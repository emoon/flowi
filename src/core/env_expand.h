#pragma once

#include "string.h"

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Environment Variable Expansion
//
// Syntax:
//   ${VAR}  - Braced form (ends at closing brace)
//   $VAR    - Unbraced form (ends at non-alphanumeric/underscore)
//
// Undefined variables are kept as-is, e.g. "${MISSING}" → "${MISSING}".

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns a new string allocated from arena, or the input unchanged when it holds no variables.
FlString expand_env_vars(struct FlArena* arena, FlString input);
