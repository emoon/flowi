#include "atlas.h"
#include <stdio.h>
#include <limits.h>
#include <flowi/error.h>
#include "allocator.h"
#include "internal.h"
#include "render.h"
#include "types.h"

#define PRE_ALLOC_COUNT 256

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Original code base on Mikko Mononen fontstash.h
//
// Copyright (c) 2009-2013 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//	claim that you wrote the original software. If you use this software
//	in a product, an acknowledgment in the product documentation would be
//	appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//	misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool insert_node(Atlas* self, int idx, int x, int y, int w) {
    int i;
    // Insert node
    if (self->count + 1 > self->capacity) {
        self->capacity = self->capacity == 0 ? 8 : self->capacity * 2;
        self->nodes = (AtlasNode*)FlAllocator_realloc(self->allocator, self->nodes, sizeof(AtlasNode) * self->capacity);
        if (self->nodes == NULL) {
            ERROR_ADD(FlError_Memory, "Out of memory in Atlas: %s", "atlas");
            return false;
        }
    }

    for (i = self->count; i > idx; i--)
        self->nodes[i] = self->nodes[i - 1];

    self->nodes[idx].x = (short)x;
    self->nodes[idx].y = (short)y;
    self->nodes[idx].width = (short)w;
    self->count++;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void remove_node(Atlas* self, int idx) {
    int count = self->count - 1;

    if (count <= 0) {
        return;
    }

    for (int i = idx; i < count; i++) {
        self->nodes[i] = self->nodes[i + 1];
    }

    self->count = count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Atlas_expand(Atlas* self, int w, int h) {
    // Insert node for empty space
    if (w > self->width) {
        if (!insert_node(self, self->count, self->width, 0, w - self->width)) {
            return false;
        }
    }

    const int memory_size = w * h * self->image_stride_mul;

    u8* image_data = FlAllocator_alloc(self->allocator, memory_size);
    if (!image_data) {
        ERROR_ADD(FlError_Memory, "Out of memory expanding Atlas: %d size", memory_size);
        return false;
    }

    u8* dest = image_data;
    u8* src = self->image_data;

    // copy the old image data to the new
    const int old_stride = self->image_stride;
    const int new_stride = w * self->image_stride_mul;

    for (int i = 0, old_h = self->height; i < old_h; ++i) {
        memcpy(dest, src, old_stride);
        dest += new_stride;
        src += old_stride;
    }

    FlAllocator_free(self->allocator, self->image_data);

    self->image_data = image_data;
    self->width = w;
    self->height = h;
    self->image_stride = new_stride;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Atlas_reset(Atlas* self, int w, int h) {
    self->width = w;
    self->height = h;
    self->count = 0;

    // Init root node.
    self->nodes[0].x = 0;
    self->nodes[0].y = 0;
    self->nodes[0].width = (short)w;
    self->count = 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool add_skyline_level(Atlas* self, int idx, int x, int y, int w, int h) {
    // Insert new node
    if (!insert_node(self, idx, x, y + h, w)) {
        return false;
    }

    // Delete skyline segments that fall under the shadow of the new segment.
    for (int i = idx + 1; i < self->count; i++) {
        if (self->nodes[i].x < self->nodes[i - 1].x + self->nodes[i - 1].width) {
            int shrink = self->nodes[i - 1].x + self->nodes[i - 1].width - self->nodes[i].x;
            self->nodes[i].x += (short)shrink;
            self->nodes[i].width -= (short)shrink;
            if (self->nodes[i].width <= 0) {
                remove_node(self, i);
                i--;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    // Merge same height skyline segments that are next to each other.
    // Can't cache self->count as remove node changes the value
    for (int i = 0; i < self->count - 1; i++) {
        if (self->nodes[i].y == self->nodes[i + 1].y) {
            self->nodes[i].width += self->nodes[i + 1].width;
            remove_node(self, i + 1);
            i--;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checks if there is enough space at the location of skyline span 'i',
// and return the max height of all skyline spans under that at that location,
// (think tetris block being dropped at that position). Or -1 if no space found.

static int rect_fits(const Atlas* self, int i, int w, int h) {
    const int x = self->nodes[i].x;
    const int count = self->count;
    const int height = self->height;

    int y = self->nodes[i].y;
    int space_left = w;

    if (x + w > self->width) {
        return -1;
    }

    while (space_left > 0) {
        if (i == count)
            return -1;

        y = FL_MAX(y, self->nodes[i].y);

        if (y + h > height)
            return -1;

        space_left -= self->nodes[i].width;
        ++i;
    }

    return y;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u8* Atlas_add_rect(Atlas* self, int rw, int rh, int* rx, int* ry, int* image_stride) {
    int besth = self->height, bestw = self->width, besti = -1;
    int bestx = -1, besty = -1, i;
    const int count = self->count;

    // Bottom left fit heuristic.
    for (i = 0; i < count; i++) {
        int y = rect_fits(self, i, rw, rh);
        if (y != -1) {
            if (y + rh < besth || (y + rh == besth && self->nodes[i].width < bestw)) {
                besti = i;
                bestw = self->nodes[i].width;
                besth = y + rh;
                bestx = self->nodes[i].x;
                besty = y;
            }
        }
    }

    if (besti == -1) {
        return NULL;
    }

    // Perform the actual packing.
    if (!add_skyline_level(self, besti, bestx, besty, rw, rh)) {
        return NULL;
    }

    *rx = bestx;
    *ry = besty;
    *image_stride = self->image_stride;

    self->dirty_rect.x0 = FL_MIN(self->dirty_rect.x0, bestx);
    self->dirty_rect.y0 = FL_MIN(self->dirty_rect.y0, besty);
    self->dirty_rect.x1 = FL_MAX(self->dirty_rect.x1, bestx + rw);
    self->dirty_rect.y1 = FL_MAX(self->dirty_rect.y1, besty + rh);

    printf("atlas offset write: %d %d: %d\n", bestx, besty, self->image_stride);

    // Return star ptr to fill with image data
    u8* image_data = self->image_data + (besty * self->image_stride * 4) + bestx * 4;

    return image_data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Used when starting to add/end rects to track dirty area

void Atlas_begin_add_rects(Atlas* self) {
    self->dirty_rect.x0 = INT_MAX;
    self->dirty_rect.y0 = INT_MAX;
    self->dirty_rect.x1 = INT_MIN;
    self->dirty_rect.y1 = INT_MIN;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Atlas_end_add_rects(Atlas* self, FlGlobalState* state) {
    FL_UNUSED(state);
    // Validate that we actually have a rect to update
    if (self->dirty_rect.x1 < 0 || self->dirty_rect.x0 >= self->width) {
        return;
    }

    //printf("atlas dirty rect %d %d %d %d\n", self->dirty_rect.x0, self->dirty_rect.y0, self->dirty_rect.x1, self->dirty_rect.y1);

    // Add command to update the texture with the added image(s) 
    FlUpdateTexture* cmd = Render_update_texture_cmd(state);
    cmd->data = self->image_data;
    cmd->rect = self->dirty_rect;
    cmd->texture_id = self->texture_id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Atlas_destroy(Atlas* self) {
    FlAllocator* allocator = self->allocator;
    FlAllocator_free(allocator, self->image_data);
    FlAllocator_free(allocator, self->nodes);
    FlAllocator_free(allocator, self);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Atlas* Atlas_create(int w, int h, AtlasImageType image_type, struct FlGlobalState* state, FlAllocator* allocator) {
    int image_stride_mul = 4;

    if (image_type == AtlasImageType_U8) {
        image_stride_mul = 1;
    } else if (image_type == AtlasImageType_RGBA8) {
        image_stride_mul = 4;
    } else {
        ERROR_ADD(FlError_Memory, "Invalid image type %d", image_type);
        return NULL;
    }

    const int image_memory_size = w * h * image_stride_mul;

    u8* image_data = FlAllocator_alloc(allocator, image_memory_size);
    if (!image_data) {
        ERROR_ADD(FlError_Memory, "Unable to allocate memory for Atlas: %d", image_memory_size);
        return NULL;
    }

    memset(image_data, 0xff, image_memory_size);

    Atlas* self = FlAllocator_alloc_zero_type(allocator, Atlas);
    if (!self) {
        ERROR_ADD(FlError_Memory, "Unable to create atlas: %s", "out of memory");
        return NULL;
    }

    self->allocator = allocator;
    self->nodes = FlAllocator_alloc_array_type(allocator, PRE_ALLOC_COUNT, AtlasNode);
    self->capacity = PRE_ALLOC_COUNT;
    self->image_type = image_type;
    self->image_stride = w;  // * image_stride_mul; TODO: Investigate why this isn't the case
    self->image_stride_mul = image_stride_mul;
    self->image_data = image_data;

    if (!self->nodes) {
        ERROR_ADD(FlError_Memory, "Unable to create atlas nodes: %s", "out of memory");
        Atlas_destroy(self);
        return NULL;
    }

    FL_UNUSED(state);

    // Create texture

    FlCreateTexture* texture = Render_create_texture_cmd(state);

    if (image_type == AtlasImageType_U8) {
        texture->format = FlTextureFormat_R8Linear;
    } else if (image_type == AtlasImageType_RGBA8) {
        texture->format = FlTextureFormat_Rgba8Srgb;
    } else {
        ERROR_ADD(FlError_Generic, "AtlasCrate: Unsupported texture format: %d", image_type);
        return NULL;
    }

    texture->data = NULL;
    texture->id = state->texture_ids++;  // TODO: Function
    texture->width = w;
    texture->height = h;

    self->texture_id = texture->id;

    Atlas_reset(self, w, h);

    return self;
}
