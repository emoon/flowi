#include "dock_panels.h"

#include <core/string.h>

DockPanel* dock_panels_find(const DockPanels* panels, FlDockPanelId id) {
    for (u32 i = 0; i < panels->count; ++i) {
        if (panels->items[i].id == id) {
            return &panels->items[i];
        }
    }
    return nullptr;
}

DockPanel* dock_panels_get(DockPanels* panels, FlDockPanelId id) {
    DockPanel* existing = dock_panels_find(panels, id);
    if (existing != nullptr) {
        return existing;
    }
    panels->items = (DockPanel*)dock_arena_array_reserve(panels->arena, panels->items, panels->count, &panels->cap,
                                                         sizeof(DockPanel), panels->count + 1);
    DockPanel* panel = &panels->items[panels->count++];
    *panel = (DockPanel) { .id = id, .title = string_empty() };
    return panel;
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
    for (u32 i = 0; i < leaf->leaf.count; ++i) {
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
