#include "atlas.h"
#include "types.h"
#include "allocator.h"
#include "../include/error.h"
#include "internal.h"

#define PRE_ALLOC_COUNT 256

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool insert_node(Atlas* atlas, int idx, int x, int y, int w) {
    int i;
    // Insert node
    if (atlas->count + 1 > atlas->capacity) {
        atlas->capacity = atlas->capacity == 0 ? 8 : atlas->capacity * 2;
        atlas->nodes = (AtlasNode*)FlAllocator_realloc(atlas->allocator, atlas->nodes, sizeof(AtlasNode) * atlas->capacity);
        printf("insert_node %d\n", atlas->count);
        if (atlas->nodes == NULL) {
        	ERROR_ADD(FlError_Memory, "Out of memory in Atlas: %s", "atlas");
            return false;
        }
    }

    for (i = atlas->count; i > idx; i--)
        atlas->nodes[i] = atlas->nodes[i - 1];

    atlas->nodes[idx].x = (short)x;
    atlas->nodes[idx].y = (short)y;
    atlas->nodes[idx].width = (short)w;
    atlas->count++;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void remove_node(Atlas* atlas, int idx) {
    if (atlas->count == 0) {
        return;
    }

    for (int i = idx, count = atlas->count - 1; i < count; i++) {
        atlas->nodes[i] = atlas->nodes[i + 1];
    }

    atlas->count--;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Atlas_expand(Atlas* atlas, int w, int h) {
    // Insert node for empty space
    if (w > atlas->width) {
        if (!insert_node(atlas, atlas->count, atlas->width, 0, w - atlas->width)) {
        	return false;
        }
    }

    const int memory_size = w * h * atlas->image_stride_mul;

	u8* image_data = FlAllocator_alloc(atlas->allocator, memory_size);
	if (!image_data) {
		ERROR_ADD(FlError_Memory, "Out of memory expanding Atlas: %d size", memory_size);
		return false;
	}

	u8* dest = image_data;
	u8* src = atlas->image_data;

    // copy the old image data to the new

	const int old_stride = atlas->image_stride;
	const int new_stride = w * atlas->image_stride_mul;

	for (int i = 0, old_h = atlas->height; i < old_h; ++i) {
		memcpy(dest, src, old_stride);
		dest += new_stride;
		src += old_stride;
	}

	FlAllocator_free(atlas->allocator, atlas->image_data);

	atlas->image_data = image_data;

    atlas->width = w;
    atlas->height = h;
    atlas->image_stride = 0;//new_stride;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Atlas_reset(Atlas* atlas, int w, int h) {
    atlas->width = w;
    atlas->height = h;
    atlas->count = 0;

    // Init root node.
    atlas->nodes[0].x = 0;
    atlas->nodes[0].y = 0;
    atlas->nodes[0].width = (short)w;
    atlas->count = 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool add_skyline_level(Atlas* atlas, int idx, int x, int y, int w, int h) {
    int i, count;

    // Insert new node
    if (!insert_node(atlas, idx, x, y + h, w)) {
        return false;
    }

    // Delete skyline segments that fall under the shadow of the new segment.
    for (i = idx + 1, count = atlas->count; i < count; i++) {
        if (atlas->nodes[i].x < atlas->nodes[i - 1].x + atlas->nodes[i - 1].width) {
            int shrink = atlas->nodes[i - 1].x + atlas->nodes[i - 1].width - atlas->nodes[i].x;
            atlas->nodes[i].x += (short)shrink;
            atlas->nodes[i].width -= (short)shrink;
            if (atlas->nodes[i].width <= 0) {
                remove_node(atlas, i);
                i--;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    // Merge same height skyline segments that are next to each other.
    for (i = 0; i < atlas->count - 1; i++) {
        if (atlas->nodes[i].y == atlas->nodes[i + 1].y) {
            atlas->nodes[i].width += atlas->nodes[i + 1].width;
            remove_node(atlas, i + 1);
            i--;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int rect_fits(const Atlas* atlas, int i, int w, int h) {
    // Checks if there is enough space at the location of skyline span 'i',
    // and return the max height of all skyline spans under that at that location,
    // (think tetris block being dropped at that position). Or -1 if no space found.

    const int x = atlas->nodes[i].x;
    const int count = atlas->count;
    const int height = atlas->height;

    int y = atlas->nodes[i].y;
    int space_left = w;

    if (x + w > atlas->width) {
        return -1;
    }

    while (space_left > 0) {
        if (i == count)
            return -1;

        y = FL_MAX(y, atlas->nodes[i].y);

        if (y + h > height)
            return -1;

        space_left -= atlas->nodes[i].width;
        ++i;
    }

    return y;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u8* Atlas_add_rect(Atlas* atlas, int rw, int rh, int* rx, int* ry, int* image_stride) {
    int besth = atlas->height, bestw = atlas->width, besti = -1;
    int bestx = -1, besty = -1, i;
    const int count = atlas->count;

    // Bottom left fit heuristic.
    for (i = 0; i < count; i++) {
        int y = rect_fits(atlas, i, rw, rh);
        if (y != -1) {
            if (y + rh < besth || (y + rh == besth && atlas->nodes[i].width < bestw)) {
                besti = i;
                bestw = atlas->nodes[i].width;
                besth = y + rh;
                bestx = atlas->nodes[i].x;
                besty = y;
            }
        }
    }

    if (besti == -1) {
        return NULL;
    }

    // Perform the actual packing.
    if (!add_skyline_level(atlas, besti, bestx, besty, rw, rh)) {
        return NULL;
    }

    *rx = bestx;
    *ry = besty;
    *image_stride = atlas->image_stride;

	// Return star ptr to fill with image data
    u8* image_data = atlas->image_data + (besty * atlas->image_stride) + bestx;

    return image_data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Atlas_destroy(Atlas* atlas) {
	FlAllocator* allocator = atlas->allocator;
	FlAllocator_free(allocator, atlas->nodes);
	FlAllocator_free(allocator, atlas);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Atlas* Atlas_create(int w, int h, AtlasImageType image_type, FlAllocator* allocator) {
	int image_stride_mul = 0;

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

	Atlas* atlas = FlAllocator_alloc_zero_type(allocator, Atlas);
	if (!atlas) {
		ERROR_ADD(FlError_Memory, "Unable to create atlas: %s", "out of memory");
		return NULL;
	}

	atlas->allocator = allocator;
	atlas->nodes = FlAllocator_alloc_array_type(allocator, PRE_ALLOC_COUNT, AtlasNode);
	atlas->capacity = PRE_ALLOC_COUNT;
	atlas->image_type = image_type;
	atlas->image_stride = w * image_stride_mul;
	atlas->image_stride_mul = image_stride_mul;
	atlas->image_data = image_data;

	if (!atlas->nodes) {
		ERROR_ADD(FlError_Memory, "Unable to create atlas nodes: %s", "out of memory");
		Atlas_destroy(atlas);
		atlas = NULL;
	}

	Atlas_reset(atlas, w, h);

	return atlas;
}


