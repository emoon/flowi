#pragma once

// Dock Panels: what a workspace keeps about each Detachable Panel between frames, keyed by the panel's stable id.
// The dock tree holds where a panel sits; this holds how it shows there (its tab title, whether the app hid it) and
// the workspace's own bookkeeping for it. The layout walk reads it and never writes it.

#include <flowi/core/string.h>

#include "dock_tree.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DockPanel {
    FlDockPanelId id;
    u64 last_declared; // the workspace frame of the most recent declaration; 0 = never declared
    u32 family;        // the panel's dock-family tag (0 = general); gates which workspaces accept it
    FlString title;    // the tab's label, copied on change
    bool hidden;       // no tab, no content, slot kept
    u32 overflow_logs; // overflow errors emitted for this panel's body; the report path emits at most one
} DockPanel;

typedef struct DockPanels {
    FlArena* arena; // borrowed from the owning workspace; records and titles are allocated here
    DockPanel* items;
    u32 count;
    u32 cap;
} DockPanels;

// The record for id, or nullptr for a panel never seen.
DockPanel* dock_panels_find(const DockPanels* panels, FlDockPanelId id);

// The record for id, added on first sight as a visible, untitled, general panel.
DockPanel* dock_panels_get(DockPanels* panels, FlDockPanelId id);

// True unless the app hid id. A panel never seen is visible.
bool dock_panels_visible(const DockPanels* panels, FlDockPanelId id);

// The tab label id was last declared under, or empty.
FlString dock_panels_title(const DockPanels* panels, FlDockPanelId id);

// The panel leaf shows as its selected tab: its logical selection when that panel is visible, else the first
// visible panel in tab order; 0 when every panel in the leaf is hidden. The logical selection is never rewritten,
// so re-showing a hidden panel restores the leaf exactly.
FlDockPanelId dock_panels_shown_in_leaf(const DockPanels* panels, const FlDockNode* leaf);

#ifdef __cplusplus
}
#endif
