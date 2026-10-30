use flowi::{Application, Dock, DockSide};

mod common;

const WIN_W: u32 = 800;
const WIN_H: u32 = 600;

// Stable panel ids: location-independent identity, never interpreted as data.
const NAVIGATOR: u64 = 1;
const LOG: u64 = 2;
const VIEWPORT: u64 = 3;

#[derive(Default)]
struct DockProbe {
    built: bool,
    nav_leaf: u32,
    log_leaf: u32,
    view_leaf: u32,
    nav_emits: bool,
    log_emits: bool,
    view_emits: bool,
    log_hidden_emits: bool,
    log_visible: bool,
    absent_reserved: bool,
    nav_family: u32,
    filter: u32,
}

fn frame(p: &mut DockProbe) {
    if !p.built {
        // LOG ends up the selected tab of the root leaf, being appended last.
        Dock::add_panel(NAVIGATOR);
        Dock::add_panel(LOG);
        Dock::dock_outer(VIEWPORT, DockSide::Right);
        Dock::set_panel_family(NAVIGATOR, 7);
        Dock::set_family_filter(7);
        p.built = true;
    }

    Dock::workspace_begin();
    let _nav_run = Dock::panel(NAVIGATOR, "Navigator");
    let _log_run = Dock::panel(LOG, "Log");
    let _view_run = Dock::panel(VIEWPORT, "Viewport");
    Dock::workspace_end();

    p.nav_leaf = Dock::panel_leaf(NAVIGATOR);
    p.log_leaf = Dock::panel_leaf(LOG);
    p.view_leaf = Dock::panel_leaf(VIEWPORT);
    p.nav_emits = Dock::panel_should_emit(NAVIGATOR);
    p.log_emits = Dock::panel_should_emit(LOG);
    p.view_emits = Dock::panel_should_emit(VIEWPORT);
    p.absent_reserved = Dock::panel_reserved(999);
    p.nav_family = Dock::panel_family(NAVIGATOR);
    p.filter = Dock::family_filter();

    Dock::set_panel_visible(LOG, false);
    p.log_hidden_emits = Dock::panel_should_emit(LOG);
    Dock::set_panel_visible(LOG, true);
    p.log_visible = Dock::panel_visible(LOG);
}

#[test]
fn declares_panels_into_docked_workspace() {
    let mut app = Application::new(WIN_W, WIN_H, true);
    let mut p = DockProbe::default();
    common::tick_n(&mut app, &mut p, frame, 4, "headless");

    assert_ne!(p.nav_leaf, 0, "navigator bound to a leaf");
    assert_ne!(p.log_leaf, 0, "log bound to a leaf");
    assert_ne!(p.view_leaf, 0, "viewport bound to a leaf");

    assert_eq!(
        p.nav_leaf, p.log_leaf,
        "navigator + log share a tab-group leaf"
    );
    assert_ne!(
        p.view_leaf, p.nav_leaf,
        "viewport split into its own region"
    );

    // Only a leaf's selected tab emits.
    assert!(p.log_emits, "log is the selected tab of its group");
    assert!(
        !p.nav_emits,
        "navigator is the background tab — it does not emit"
    );
    assert!(p.view_emits, "viewport is its leaf's selected tab");

    // Visibility is the app's own bit, distinct from the emit decision: a hidden panel never emits, and showing
    // it again restores the leaf exactly.
    assert!(!p.log_hidden_emits, "a hidden panel does not emit");
    assert!(
        p.log_visible,
        "visibility reads back after showing the panel again"
    );

    assert!(!p.absent_reserved, "unknown panel is not reserved");

    assert_eq!(p.nav_family, 7, "navigator family tag round-trips");
    assert_eq!(p.filter, 7, "workspace family filter round-trips");
}
