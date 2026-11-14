#include "dock_panels.h"

#include <core/string.h>

const DockPanel* dock_panels_find(const DockPanels* panels, FlDockPanelId id) {
    for_count(i, panels->count) {
        if (panels->items[i].id == id) {
            return &panels->items[i];
        }
    }
    return nullptr;
}

DockPanel* dock_panels_get(DockPanels* panels, FlDockPanelId id) {
    for_count(i, panels->count) {
        if (panels->items[i].id == id) {
            return &panels->items[i];
        }
    }
    panels->items = (DockPanel*)dock_arena_array_reserve(panels->arena, panels->items, panels->count, &panels->cap,
                                                         sizeof(DockPanel), panels->count + 1);
    DockPanel* panel = &panels->items[panels->count++];
    *panel = (DockPanel) { .id = id, .title = string_empty() };
    return panel;
}

void dock_panels_set_title(DockPanels* panels, DockPanel* panel, FlString title) {
    if (!fl_string_equals(panel->title, title)) {
        panel->title = fl_string_copy(panels->arena, title);
    }
}

bool dock_panels_visible(const DockPanels* panels, FlDockPanelId id) {
    const DockPanel* panel = dock_panels_find(panels, id);
    return panel == nullptr || !panel->hidden;
}

FlString dock_panels_title(const DockPanels* panels, FlDockPanelId id) {
    const DockPanel* panel = dock_panels_find(panels, id);
    return panel != nullptr ? panel->title : string_empty();
}

FlDockPanelId dock_panels_shown_in_leaf(const DockPanels* panels, const FlDockNode* leaf) {
    FlDockPanelId first_shown = 0;
    for_count(i, leaf->leaf.count) {
        FlDockPanelId panel = leaf->leaf.panels[i];
        if (!dock_panels_visible(panels, panel)) {
            continue;
        }
        if (i == leaf->leaf.selected) {
            return panel;
        }
        if (first_shown == 0) {
            first_shown = panel;
        }
    }
    return first_shown;
}
