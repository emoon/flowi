#include "../include/error.h"
#include "internal.h"
#include "types.h"

// fix malloc
#include <stdlib.h>

// TODO: Support io-override
#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FILE* open_file(const char* filename, const char* mode) {
#ifdef _WIN32
    int file_len = strlen(filename);
    int mode_len = strlen(mode);
    wchar_t wpath[MAX_PATH];
    wchar_t wmode[MAX_PATH];

    if (file_len == 0) {
        return NULL;
    }

    if (mode_len == 0) {
        return NULL;
    }

    int len = MultiByteToWideChar(CP_UTF8, 0, filename, file_len, wpath, file_len);
    if (len >= MAX_PATH) {
    	// TODO: Error
        return NULL;
    }

    wpath[len] = L'\0';
    len = MultiByteToWideChar(CP_UTF8, 0, mode, mode_len, wmode, mode_len);
    if (len >= MAX_PATH) {
    	// TODO: Error
        return NULL;
    }

    wmode[len] = L'\0';
	return _wfopen(wpath, wmode);
#else
    return fopen(filename, mode);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO: Add support to override IO functions
// TODO: Add allocator

u8* Io_load_file_to_memory(const char* filename, u32* out_size) {
    // TODO: Use internal/sandboxed allocator
    FILE* f = open_file(filename, "rb");
    u8* data = NULL;
    *out_size = 0;

    if (!f) {
        ERROR_ADD(FlError_Io, "Unable to open %s for reading", filename);
        goto cleanup;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        ERROR_ADD(FlError_Io, "Unable to seek to end %s for reading", filename);
        goto cleanup;
    }

    s64 filesize = (s64)ftell(f);

    if (filesize == -1) {
        ERROR_ADD(FlError_Io, "Unable to get size for file %s", filename);
        goto cleanup;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        ERROR_ADD(FlError_Io, "Unable to seek to start %s for reading", filename);
        goto cleanup;
    }

    // TODO: sandboxed allocator
    data = malloc(filesize);

    if (!data) {
        ERROR_ADD(FlError_Memory, "Unable to allocated %d bytes for reading file %s to memory", filesize, filename);
        goto cleanup;
    }

    if (fread(data, 1, filesize, f) != (size_t)filesize) {
        ERROR_ADD(FlError_Io, "Unable to read the whole %s file to memory, size %d", filename, filesize);
        goto cleanup;
    }

    *out_size = (u32)filesize;

cleanup:
    if (f) {
        fclose(f);
    }

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Errors_add(FlError err, const char* filename, int line, const char* fmt, ...) {
    FL_UNUSED(err);
    FL_UNUSED(filename);
    FL_UNUSED(line);
    FL_UNUSED(fmt);
}
