//! A menu item lays out under the (label, per-level-index) id the C core hashes, so the same (name, seed)
//! query - element_state(("Cut", 0)) - finds its rect.

use flowi::{element_state, menu, menu_item, menu_region, Application, ElementState, Menu};

mod common;

// Large enough that the menu opens downward unclamped; ui_scale is 1.0, so window px equal logical px.
const WIN_W: u32 = 800;
const WIN_H: u32 = 600;
// Right-click point inside the region (well clear of the edges).
const OPEN_X: i32 = 120;
const OPEN_Y: i32 = 120;

#[derive(Default)]
struct MenuProbe {
    /// Menu::is_open snapshot taken after the menu block each frame.
    menu_open: bool,
    cut_clicked: bool,
    /// Captured inside the frame: element_state needs an active frame.
    cut: Option<ElementState>,
}

// The region grows to fill the window. The menu! body runs only while the menu is open.
fn frame(p: &mut MenuProbe) {
    menu_region! { "ctx_region", "ctx_menu" {} }
    menu! { "ctx_menu", min_width: 200.0 {
        if menu_item!("Cut").clicked {
            p.cut_clicked = true;
        }
    }}
    p.menu_open = Menu::is_open("ctx_menu");
    // Reports the previous frame's resolved layout.
    p.cut = Some(element_state(("Cut", 0)));
}

#[test]
fn headless_context_menu_click() {
    let mut app = Application::new(WIN_W, WIN_H, true);
    let mut probe = MenuProbe::default();

    assert!(app.tick(&mut probe, frame), "initial layout tick");
    assert!(
        !probe.menu_open,
        "menu starts closed before any right-click"
    );

    common::right_click_at(&mut app, &mut probe, frame, OPEN_X, OPEN_Y);
    assert!(probe.menu_open, "right-click in the region opened the menu");

    // element_state reports the previous frame's resolved rect, so settle a tick before reading.
    assert!(
        app.tick(&mut probe, frame),
        "settle so the menu item lays out"
    );
    let cut = probe
        .cut
        .expect("frame populates the Cut item state every tick");
    assert!(
        cut.found,
        "Cut item should be laid out while the menu is open"
    );
    assert!(
        cut.size.x > 0.0 && cut.size.y > 0.0,
        "Cut item should have a non-empty rect"
    );
    let (cx, cy) = common::center(cut);

    common::click_at(&mut app, &mut probe, frame, cx, cy);
    assert!(
        probe.cut_clicked,
        "menu_item!(\"Cut\").clicked should fire on a click over it"
    );
    assert!(
        !probe.menu_open,
        "the default close policy tore the menu down on activation"
    );
}
