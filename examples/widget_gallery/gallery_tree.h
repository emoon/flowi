#pragma once

#include <core/types.h>
#include <flowi/core/string.h>
#include <flowi/ui/tree.h>

#define GALLERY_TREE_NODES 12
#define GALLERY_VTREE_EXPANDED_MAX 64

typedef struct GalleryTree {
    FlSelectionSet* selection;
    bool expanded[GALLERY_TREE_NODES];
} GalleryTree;

typedef struct GalleryVirtualTree {
    FlSelectionSet* selection;
    FlTreeExpanded expanded[GALLERY_VTREE_EXPANDED_MAX];
    u32 expanded_count;
} GalleryVirtualTree;

void gallery_tree_init(GalleryTree* tree);
void gallery_tree_destroy(GalleryTree* tree);
void gallery_tree_show(GalleryTree* tree);

void gallery_virtual_tree_init(GalleryVirtualTree* tree);
void gallery_virtual_tree_destroy(GalleryVirtualTree* tree);
void gallery_virtual_tree_show(GalleryVirtualTree* tree);
