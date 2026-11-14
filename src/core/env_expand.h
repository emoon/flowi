#pragma once

#include "string.h"

struct FlArena;

// Syntax:
//   ${VAR}  braced form, ends at the closing brace
//   $VAR    unbraced form, ends at the first character that is not alphanumeric or an underscore
//
// Undefined variables are kept as written, e.g. "${MISSING}" stays "${MISSING}".

// Returns a new string allocated from arena, or the input unchanged when it holds no variables.
FlString expand_env_vars(struct FlArena* arena, FlString input);
