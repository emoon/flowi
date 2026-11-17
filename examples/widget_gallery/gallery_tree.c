#include "gallery_tree.h"

#include <stdio.h>
#include <string.h>

#include <core/core.h>
#include <flowi/ui/cell.h>
#include <flowi/ui/frame.h>
#include <flowi/ui/theme.h>
#include "ui/layout.h"
#include "ui/ui.h"
#include "ui/ui_list.h"
#include "ui/ui_selection.h"

#define GALLERY_TREE_HEIGHT 180.0f
#define GALLERY_TREE_ROW_HEIGHT 28.0f
#define GALLERY_TREE_GAP 2.0f
#define GALLERY_VTREE_ROOTS 3u
#define GALLERY_VTREE_BIG_ROOT 1u
#define GALLERY_VTREE_BIG_COUNT 1000000u
#define GALLERY_VTREE_SMALL_COUNT 3u
#define GALLERY_VTREE_LEAVES 4u

enum { TreeColumn_Name = 1, TreeColumn_Kind, TreeColumn_Size };

typedef struct GalleryNode {
    FlString label;
    u16 depth;
    FlString kind;
    FlString size;
} GalleryNode;

static const GalleryNode s_nodes[GALLERY_TREE_NODES] = {
    { FL_S_("Assets"), 0, FL_S_("Folder"), FL_S_("2 items") },
    { FL_S_("Audio"), 1, FL_S_("Folder"), FL_S_("2 items") },
    { FL_S_("kick.wav"), 2, FL_S_("Wave audio"), FL_S_("412 KB") },
    { FL_S_("snare.wav"), 2, FL_S_("Wave audio"), FL_S_("388 KB") },
    { FL_S_("Textures"), 1, FL_S_("Folder"), FL_S_("2 items") },
    { FL_S_("grass.png"), 2, FL_S_("PNG image"), FL_S_("1.2 MB") },
    { FL_S_("stone.png"), 2, FL_S_("PNG image"), FL_S_("2.4 MB") },
    { FL_S_("Scripts"), 0, FL_S_("Folder"), FL_S_("2 items") },
    { FL_S_("player.lua"), 1, FL_S_("Lua script"), FL_S_("9 KB") },
    { FL_S_("enemy.lua"), 1, FL_S_("Lua script"), FL_S_("14 KB") },
    { FL_S_("Docs"), 0, FL_S_("Folder"), FL_S_("1 item") },
    { FL_S_("readme.md"), 1, FL_S_("Markdown"), FL_S_("3 KB") },
};

typedef struct VisibleRows {
    FlTreeRow rows[GALLERY_TREE_NODES];
    FlString labels[GALLERY_TREE_NODES];
    u32 count;
} VisibleRows;

static void status_text(u64 selected, u32 expanded, const char* limit_word, u32 limit) {
    char buf[80];
    snprintf(buf, sizeof(buf), "%llu selected, %u expanded (%s %u)", (unsigned long long)selected, expanded, limit_word,
             limit);
    ui_text(fl_frame_str(buf, strlen(buf)));
}

static void row_text(FlString text) {
    ui_text_styled(text, .color = fl_theme_colors()->text.primary);
}

static bool node_has_children(u32 index) {
    return index + 1 < GALLERY_TREE_NODES && s_nodes[index + 1].depth > s_nodes[index].depth;
}

static VisibleRows visible_rows(const GalleryTree* tree) {
    VisibleRows visible = { 0 };
    u16 hidden_below = UINT16_MAX;
    for_count(i, GALLERY_TREE_NODES) {
        u16 depth = s_nodes[i].depth;
        if (depth > hidden_below) {
            continue;
        }
        bool has_children = node_has_children(i);
        hidden_below = has_children && !tree->expanded[i] ? depth : UINT16_MAX;
        visible.labels[visible.count] = s_nodes[i].label;
        visible.rows[visible.count++] = (FlTreeRow) {
            .id = (u64)i + 1,
            .depth = depth,
            .has_children = has_children,
            .expanded = tree->expanded[i],
        };
    }
    return visible;
}

static u32 expanded_nodes(const GalleryTree* tree) {
    u32 count = 0;
    for_count(i, GALLERY_TREE_NODES) {
        count += tree->expanded[i] ? 1 : 0;
    }
    return count;
}

static u32 expandable_nodes(void) {
    u32 count = 0;
    for_count(i, GALLERY_TREE_NODES) {
        count += node_has_children(i) ? 1 : 0;
    }
    return count;
}

void gallery_tree_init(GalleryTree* tree) {
    tree->selection = selection_set_create(16);
    tree->expanded[0] = true;
}

void gallery_tree_destroy(GalleryTree* tree) {
    selection_set_destroy(tree->selection);
}

void gallery_tree_show(GalleryTree* tree) {
    VisibleRows visible = visible_rows(tree);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(GALLERY_TREE_HEIGHT) },
                   .direction = LayoutDirection_TopToBottom) {
        FlListRange range = fl_tree_begin(ID("gallery_tree"), visible.rows, visible.count, tree->selection,
                                          .list = { .row_height = GALLERY_TREE_ROW_HEIGHT,
                                                    .gap = GALLERY_TREE_GAP,
                                                    .labels = visible.labels,
                                                    .scrollbar_mode = FlScrollbarDisplayMode_Auto });
        for (u32 row = range.start; row < range.end; row++) {
            fl_tree_item_begin(row);
            row_text(visible.labels[row]);
            fl_tree_item_end();
        }
        FlTreeResult result = fl_tree_end();
        if (result.toggled_id != 0) {
            tree->expanded[result.toggled_id - 1] = result.toggle_expanded;
        }
    }
    status_text(selection_set_len(tree->selection), expanded_nodes(tree), "of", expandable_nodes());
}

void gallery_tree_table_init(GalleryTreeTable* table) {
    gallery_tree_init(&table->tree);
    table->tree.expanded[1] = true;
    table->edit.input = fl_text_input_create(GALLERY_KIND_MAX);
    for_count(i, GALLERY_TREE_NODES) {
        snprintf(table->kinds[i], GALLERY_KIND_MAX, "%.*s", (int)s_nodes[i].kind.length, s_nodes[i].kind.data);
    }
    table->columns.columns[0] = (FlColumnDef) { .id = TreeColumn_Name, .title = FL_S_("Name"), .width = 220.0f };
    table->columns.columns[1] = (FlColumnDef) { .id = TreeColumn_Kind, .title = FL_S_("Kind"), .width = 120.0f };
    table->columns.columns[2]
        = (FlColumnDef) { .id = TreeColumn_Size, .title = FL_S_("Size"), .flex = 1.0f, .min_width = 60.0f };
}

void gallery_tree_table_destroy(GalleryTreeTable* table) {
    fl_text_input_destroy(table->edit.input);
    gallery_tree_destroy(&table->tree);
}

static FlString kind_of(const GalleryTreeTable* table, u64 row_id) {
    const char* kind = table->kinds[row_id - 1];
    return fl_frame_str(kind, strlen(kind));
}

// Enter or a double-click on a row starts the edit, with the whole text selected.
static void edit_start(GalleryTreeTable* table, u64 row_id) {
    table->edit.row_id = row_id;
    table->edit.had_focus = false;
    fl_text_input_set_text(table->edit.input, kind_of(table, row_id));
    fl_text_input_request_focus(table->edit.input, true);
}

static void edit_commit(GalleryTreeTable* table) {
    FlString text = fl_text_input_get_text(table->edit.input);
    snprintf(table->kinds[table->edit.row_id - 1], GALLERY_KIND_MAX, "%.*s", (int)text.length, text.data);
}

// Ends the edit and gives the keyboard back to the tree, on the row that was edited.
static void edit_end(GalleryTreeTable* table, bool commit) {
    if (commit) {
        edit_commit(table);
    }
    fl_list_request_focus(ID("gallery_tree_table"), table->edit.row_id);
    table->edit.row_id = 0;
}

static bool edit_lost_focus(const GalleryCellEdit* edit, FlSignal signal) {
    return edit->had_focus && !signal.focused;
}

// The edit ends on Enter (commit), Escape (cancel) or when focus goes elsewhere (commit).
static void edit_cell(GalleryTreeTable* table) {
    fl_cell_begin(TreeColumn_Kind);
    FlSignal signal = fl_text_input_show(FL_S("gallery_tree_edit"), table->edit.input, .borderless = true);
    fl_cell_end();
    if (signal.cancelled) {
        edit_end(table, false);
    } else if (signal.submitted || edit_lost_focus(&table->edit, signal)) {
        edit_end(table, true);
    }
    table->edit.had_focus = signal.focused;
}

static void kind_cell(GalleryTreeTable* table, u64 row_id) {
    if (table->edit.row_id == row_id) {
        edit_cell(table);
    } else {
        gallery_cell_text(TreeColumn_Kind, kind_of(table, row_id));
    }
}

static void tree_table_row(GalleryTreeTable* table, const VisibleRows* visible, u32 row) {
    u64 row_id = visible->rows[row].id;
    const GalleryNode* node = &s_nodes[row_id - 1];
    FlSignal signal = fl_tree_item_begin(row);
    if (signal.cell != 0) {
        table->columns.hovered = signal.cell;
    }
    gallery_cell_text(TreeColumn_Name, node->label);
    kind_cell(table, row_id);
    gallery_cell_text(TreeColumn_Size, node->size);
    fl_tree_item_end();
}

static void tree_table_apply(GalleryTreeTable* table, const FlTreeResult* result) {
    if (result->toggled_id != 0) {
        table->tree.expanded[result->toggled_id - 1] = result->toggle_expanded;
    }
    if (result->list.activated_id != 0) {
        edit_start(table, result->list.activated_id);
    }
    gallery_columns_apply(&table->columns, &result->list);
}

void gallery_tree_table_show(GalleryTreeTable* table) {
    VisibleRows visible = visible_rows(&table->tree);
    table->columns.hovered = 0;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(GALLERY_TREE_HEIGHT) },
                   .direction = LayoutDirection_TopToBottom) {
        FlListRange range = fl_tree_begin(ID("gallery_tree_table"), visible.rows, visible.count, table->tree.selection,
                                          .list = { .row_height = GALLERY_TREE_ROW_HEIGHT,
                                                    .gap = GALLERY_TREE_GAP,
                                                    .columns = table->columns.columns,
                                                    .column_count = GALLERY_TABLE_COLUMNS,
                                                    .scrollbar_mode = FlScrollbarDisplayMode_Auto });
        for (u32 row = range.start; row < range.end; row++) {
            tree_table_row(table, &visible, row);
        }
        FlTreeResult result = fl_tree_end();
        tree_table_apply(table, &result);
    }
    gallery_columns_status(&table->columns);
}

static u32 virtual_child_count(const GalleryVirtualTree* tree, FlTreeVirtualRow row) {
    if (row.depth == 0) {
        return row.child_index == GALLERY_VTREE_BIG_ROOT ? GALLERY_VTREE_BIG_COUNT : GALLERY_VTREE_SMALL_COUNT;
    }
    bool under_big_root = row.depth == 1 && tree->expanded[row.parent].child_index == GALLERY_VTREE_BIG_ROOT;
    return under_big_root ? GALLERY_VTREE_LEAVES : 0;
}

static i64 child_slot_under(const GalleryVirtualTree* tree, u32 index, i32 parent) {
    i32 at = (i32)index;
    while (at >= 0 && tree->expanded[at].parent != parent) {
        at = tree->expanded[at].parent;
    }
    return at >= 0 ? (i64)tree->expanded[at].child_index : -1;
}

static u32 insert_position(const GalleryVirtualTree* tree, FlTreeVirtualRow row) {
    u32 at = (u32)(row.parent + 1);
    while (at < tree->expanded_count) {
        i64 slot = child_slot_under(tree, at, row.parent);
        if (slot < 0 || slot > (i64)row.child_index) {
            break;
        }
        at++;
    }
    return at;
}

static void virtual_expand(GalleryVirtualTree* tree, FlTreeVirtualRow row) {
    u32 child_count = virtual_child_count(tree, row);
    if (child_count == 0 || tree->expanded_count == GALLERY_VTREE_EXPANDED_MAX) {
        return;
    }
    u32 at = insert_position(tree, row);
    memmove(&tree->expanded[at + 1], &tree->expanded[at], (tree->expanded_count - at) * sizeof(FlTreeExpanded));
    tree->expanded_count++;
    for (u32 i = at + 1; i < tree->expanded_count; i++) {
        if (tree->expanded[i].parent >= (i32)at) {
            tree->expanded[i].parent++;
        }
    }
    tree->expanded[at]
        = (FlTreeExpanded) { .parent = row.parent, .child_index = row.child_index, .child_count = child_count };
}

static void virtual_collapse(GalleryVirtualTree* tree, u32 node) {
    u32 end = node + 1;
    while (end < tree->expanded_count && tree->expanded[end].parent >= (i32)node) {
        end++;
    }
    u32 removed = end - node;
    memmove(&tree->expanded[node], &tree->expanded[end], (tree->expanded_count - end) * sizeof(FlTreeExpanded));
    tree->expanded_count -= removed;
    for (u32 i = node; i < tree->expanded_count; i++) {
        if (tree->expanded[i].parent > (i32)node) {
            tree->expanded[i].parent -= (i32)removed;
        }
    }
}

static void virtual_toggle(GalleryVirtualTree* tree, FlTreeVirtualRow row, bool expand) {
    if (expand && row.node < 0) {
        virtual_expand(tree, row);
    } else if (!expand && row.node >= 0) {
        virtual_collapse(tree, (u32)row.node);
    }
}

static void virtual_row_text(FlTreeVirtualRow row) {
    static const char* const kind[] = { "Group", "Item", "Leaf" };
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %u", kind[row.depth < 2 ? row.depth : 2], row.child_index + 1);
    row_text(fl_frame_str(buf, strlen(buf)));
}

void gallery_virtual_tree_init(GalleryVirtualTree* tree) {
    tree->selection = selection_set_create(16);
}

void gallery_virtual_tree_destroy(GalleryVirtualTree* tree) {
    selection_set_destroy(tree->selection);
}

void gallery_virtual_tree_show(GalleryVirtualTree* tree) {
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(GALLERY_TREE_HEIGHT) },
                   .direction = LayoutDirection_TopToBottom) {
        FlListRange range = fl_tree_begin_virtual(ID("gallery_virtual_tree"), GALLERY_VTREE_ROOTS, tree->expanded,
                                                  tree->expanded_count, tree->selection,
                                                  .list = { .row_height = GALLERY_TREE_ROW_HEIGHT,
                                                            .gap = GALLERY_TREE_GAP,
                                                            .scrollbar_mode = FlScrollbarDisplayMode_Auto });
        for (u32 row = range.start; row < range.end; row++) {
            FlTreeVirtualRow at = fl_tree_row_at(row);
            fl_tree_item_begin_virtual(row, virtual_child_count(tree, at) > 0);
            virtual_row_text(at);
            fl_tree_item_end();
        }
        FlTreeResult result = fl_tree_end();
        if (result.toggled_id != 0) {
            virtual_toggle(tree, fl_tree_row_at(result.toggled_row), result.toggle_expanded);
        }
    }
    status_text(selection_set_len(tree->selection), tree->expanded_count, "at most", GALLERY_VTREE_EXPANDED_MAX);
}
