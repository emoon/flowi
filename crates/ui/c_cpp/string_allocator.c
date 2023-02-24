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
        str_data = LinearAllocator_alloc_array(self->frame_allocator, char, len);
    } else if (type == AllocType_Persistant) {
        str_data = FlAllocator_alloc_array_type(self->allocator, len, char);
        char** track = LinearAllocator_alloc(&self->tracking, char*);

        *track = str_data;
        self->string_count++;
    }

    memcpy(str_data, str, len);

    ret_val.str = str_data;
    ret_val.len = len;

    return ret_val;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool StringAllocator_create(StringAllocator* self, struct FlAllocator* allocator, LinearAllocator* frame_allocator) {
    memset(self, 0, sizeof(StringAllocator));

    int intial_tracking_size = 1024;

    self->allocator = allocator;
    self->frame_allocator = frame_allocator;

    LinearAllocator_create_with_allocator(&self->tracking, "string tracking allocator", allocator, intial_tracking_size,
                                          true);

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

    LinearAllocator_destroy(&self->tracking);

    self->string_count = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Frame based allocator. FlString will be invalid after next update

FlString StringAllocator_copy_cstr_frame(StringAllocator* self, const char* str) {
    return copy_cstr(self, str, 0, AllocType_Frame);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Frame based allocator. FlString will be invalid after next update

FlString StringAllocator_copy_string_frame(StringAllocator* self, FlString str) {
    return copy_cstr(self, str.str, str.len, AllocType_Frame);
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
