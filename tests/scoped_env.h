#pragma once

// A process environment variable set for the rest of a case and put back as it was when the case returns, including
// when an ASSERT returns early. A variable that was unset is unset again.

#include <core/os/os.h>
#include <stdlib.h>
#include <string.h>

typedef struct ScopedEnv {
    const char* name;
    char* saved; // the value to put back; nullptr when the variable was unset
} ScopedEnv;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline ScopedEnv scoped_env_set(const char* name, const char* value) {
    ScopedEnv scope = { .name = name, .saved = nullptr };
    const char* current = getenv_os(name);
    if (current != nullptr) {
        const size_t length = strlen(current) + 1;
        scope.saved = (char*)malloc(length);
        if (scope.saved != nullptr) {
            memcpy(scope.saved, current, length);
        }
    }
    if (value != nullptr) {
        setenv_os(name, value, 1);
    } else {
        unsetenv_os(name);
    }
    return scope;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void scoped_env_restore(ScopedEnv* scope) {
    if (scope->saved != nullptr) {
        setenv_os(scope->name, scope->saved, 1);
    } else {
        unsetenv_os(scope->name);
    }
    free(scope->saved);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Declares `var`, sets name to value (nullptr unsets it) and restores the old state when var goes out of scope.
#define SCOPED_ENV(var, name, value) \
    __attribute__((cleanup(scoped_env_restore))) ScopedEnv var = scoped_env_set((name), (value))
