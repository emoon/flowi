#pragma once

#include <core/types.h>
#include <flowi/core/string.h>
#include <flowi/ui/text_input.h>
#include <flowi/ui/tree.h>
#include "gallery_table.h"

#define GALLERY_TREE_NODES 12
#define GALLERY_VTREE_EXPANDED_MAX 64

typedef struct GalleryTree {
    FlSelectionSet* selection;
    bool expanded[GALLERY_TREE_NODES];
} GalleryTree;

#define GALLERY_KIND_MAX 32

// The row being edited (0 for none) and the text input that holds the edit.
typedef struct GalleryCellEdit {
    FlTextInputState* input;
    u64 row_id;
    bool had_focus;
} GalleryCellEdit;

typedef struct GalleryTreeTable {
    GalleryTree tree;
    GalleryColumns columns;
    GalleryCellEdit edit;
    char kinds[GALLERY_TREE_NODES][GALLERY_KIND_MAX];
} GalleryTreeTable;

typedef struct GalleryVirtualTree {
    FlSelectionSet* selection;
    FlTreeExpanded expanded[GALLERY_VTREE_EXPANDED_MAX];
    u32 expanded_count;
} GalleryVirtualTree;

void gallery_tree_init(GalleryTree* tree);
void gallery_tree_destroy(GalleryTree* tree);
void gallery_tree_show(GalleryTree* tree);

void gallery_tree_table_init(GalleryTreeTable* table);
void gallery_tree_table_destroy(GalleryTreeTable* table);
void gallery_tree_table_show(GalleryTreeTable* table);

void gallery_virtual_tree_init(GalleryVirtualTree* tree);
void gallery_virtual_tree_destroy(GalleryVirtualTree* tree);
void gallery_virtual_tree_show(GalleryVirtualTree* tree);
