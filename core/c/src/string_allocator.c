#include "string_allocator.h"
#include "allocator.h"
#include "internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum AllocType {
    AllocType_Persistant,
    AllocType_Frame,
} AllocType;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString copy_cstr(StringAllocator* self, const char* str, int len, AllocType type) {
    FlString ret_val = {0};

    if (str == NULL) {
        return ret_val;
    }

    // if len is zero we assume it's a c string
    if (len == 0) {
        len = (int)strlen(str);
    }

    char* str_data = NULL;

    if (FL_LIKELY(type == AllocType_Frame)) {
        str_data = LinearAllocator_alloc_array(&self->frame_allocator, char, len + 1);
    } else if (type == AllocType_Persistant) {
        str_data = FlAllocator_alloc_array_type(self->allocator, len + 1, char);
        char** track = LinearAllocator_alloc(&self->tracking, char*);

        if (FL_UNLIKELY(!track || !str_data)) {
            ERROR_ADD(FlError_Generic, "Unable to allocate memory for string tracking: %d", sizeof(char*));
            return ret_val;
        }

        *track = str_data;
        self->string_count++;
    }

    if (FL_UNLIKELY(!str_data)) {
        ERROR_ADD(FlError_Generic, "Unable to allocate memory for string: %s", str);
        return ret_val;
    }

    memcpy(str_data, str, len);

    // we add 0 at the end to make things easier in case we want to use some C function to print
    str_data[len] = 0;

    ret_val.str = str_data;
    ret_val.c_string = 1;
    ret_val.len = len;

    return ret_val;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool StringAllocator_create(StringAllocator* self, struct FlAllocator* allocator) {
    memset(self, 0, sizeof(StringAllocator));

    int intial_frame_allocator_size = 64 * 1024;
    int intial_tracking_size = 1024;

    self->allocator = allocator;

    if (!LinearAllocator_create_with_allocator(&self->frame_allocator, "frame string allocator", allocator,
                                               intial_frame_allocator_size, true)) {
        ERROR_ADD(FlError_Memory, "Unable to allocate %d bytes for frame allocator", intial_frame_allocator_size);
        return false;
    }

    if (!LinearAllocator_create_with_allocator(&self->tracking, "string tracking allocator", allocator,
                                               intial_tracking_size, true)) {
        ERROR_ADD(FlError_Memory, "Unable to allocate %d bytes for string tracking allocator", intial_tracking_size);
        return false;
    }

    self->string_count = 0;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void StringAllocator_destroy(StringAllocator* self) {
    char** string_ptrs = (char**)self->tracking.start_data;

    for (int i = 0, count = self->string_count; i < count; ++i) {
        char* s = string_ptrs[i];
        FlAllocator_free(self->allocator, s);
    }

    LinearAllocator_destroy(&self->frame_allocator);
    LinearAllocator_destroy(&self->tracking);

    self->string_count = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void StringAllocator_end_frame(StringAllocator* self) {
    LinearAllocator_rewind(&self->frame_allocator);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Frame based allocator. FlString will be invalid after next update

FlString StringAllocator_copy_cstr_frame(StringAllocator* self, const char* str) {
    return copy_cstr(self, str, 0, AllocType_Frame);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString StringAllocator_copy_cstr(StringAllocator* self, const char* str) {
    return copy_cstr(self, str, 0, AllocType_Persistant);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString StringAllocator_copy_string(StringAllocator* self, FlString str) {
    return copy_cstr(self, str.str, str.len, AllocType_Persistant);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Notice that this string is only valid for the local scope of the calling code

const char* StringAllocator_temp_string_to_cstr(StringAllocator* self, char* temp_buffer, int temp_len, FlString str) {
    if (str.c_string) {
        temp_buffer[0] = 0;
        return str.str;
    }

    if (str.len + 1 < (u32)temp_len) {
        memcpy(temp_buffer, str.str, str.len);
        temp_buffer[str.len] = 0;
        return temp_buffer;
    } else {
        FlString t = copy_cstr(self, str.str, str.len, AllocType_Frame);
        return t.str;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ideally this should not be called unless if some really large buffer is being removed as the tracking is slow

void StringAllocator_free_string(StringAllocator* self, FlString str) {
    char** string_ptrs = (char**)self->tracking.start_data;

    for (int i = 0, count = self->string_count; i < count; ++i) {
        char* s = string_ptrs[i];

        if (s == str.str) {
            // move the last ptr to the current position and reduce count by one
            string_ptrs[i] = string_ptrs[count - 1];
            FlAllocator_free(self->allocator, s);
            self->string_count = count - 1;
            return;
        }
    }
}
