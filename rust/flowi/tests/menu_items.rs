//! Each row lays out under the `(label, (level << 16) | index)` id the C core hashes, so an
//! element_state((label, seed)) query finds its rect. The index is the dense per-level emission order, so the
//! separator and header rows count toward it.
//!
//! The submenu is opened by clicking its parent row.

use flowi::{
    element_state, menu, menu_check, menu_header, menu_item, menu_radio, menu_region,
    menu_separator, submenu, Application, ElementState, Menu,
};

mod common;

// Large enough that the menu opens downward and the submenu rightward unclamped; ui_scale is 1.0, so window px
// equal logical px.
const WIN_W: u32 = 800;
const WIN_H: u32 = 600;
// Right-click point inside the region (well clear of the edges).
const OPEN_X: i32 = 120;
const OPEN_Y: i32 = 120;
const WAVE_VALUE: i32 = 1;
// The seed of a row at build level 1 (an open submenu's first row): `(1 << 16) | 0`.
const LEVEL_1: i32 = 1 << 16;

#[derive(Default)]
struct MenuProbe {
    bold: bool,
    wave: i32,
    child_clicked: bool,
    /// Snapshot taken inside the frame each tick.
    menu_open: bool,
    /// Each reports the previous frame's resolved layout.
    bold_row: Option<ElementState>,
    wave_row: Option<ElementState>,
    sub_row: Option<ElementState>,
    child_row: Option<ElementState>,
}

// The region grows to fill the window. The menu! body runs only while open, and a submenu's body only while
// it is expanded. Per-level emission order fixes each row's index:
// Bold 0, Wave 1, separator 2, header 3, Sub 4; Child is 0 in the nested level.
fn frame(p: &mut MenuProbe) {
    menu_region! { "ctx_region", "ctx_menu" {} }
    menu! { "ctx_menu", min_width: 200.0 {
        menu_check!("Bold", &mut p.bold);
        menu_radio!("Wave", &mut p.wave, WAVE_VALUE);
        menu_separator!();
        menu_header!("Section");
        submenu! { "Sub" {
            if menu_item!("Child").clicked {
                p.child_clicked = true;
            }
        }}
    }}
    // Child only lays out while the submenu is expanded; until then its query reports not-found.
    p.bold_row = Some(element_state(("Bold", 0)));
    p.wave_row = Some(element_state(("Wave", 1)));
    p.sub_row = Some(element_state(("Sub", 4)));
    p.child_row = Some(element_state(("Child", LEVEL_1)));
    p.menu_open = Menu::is_open("ctx_menu");
}

fn click_row(app: &mut Application, probe: &mut MenuProbe, row: ElementState) {
    let (x, y) = common::center(row);
    common::click_at(app, probe, frame, x, y);
}

fn toggle_check_item(app: &mut Application, probe: &mut MenuProbe) {
    assert!(!probe.bold, "the check item starts unchecked");
    let bold = probe
        .bold_row
        .expect("frame populates the Bold row state every tick");
    assert!(
        bold.found && bold.size.x > 0.0,
        "the Bold check item should be laid out while the menu is open"
    );
    click_row(app, probe, bold);
    assert!(
        probe.bold,
        "clicking the check item flipped its &mut bool to true"
    );
    assert!(
        probe.menu_open,
        "the check item keeps the menu open by default"
    );
}

fn select_radio_item(app: &mut Application, probe: &mut MenuProbe) {
    assert_eq!(
        probe.wave, 0,
        "the radio group state starts at its default (0, no option selected)"
    );
    let wave = probe
        .wave_row
        .expect("frame populates the Wave row state every tick");
    assert!(
        wave.found && wave.size.x > 0.0,
        "the Wave radio item should be laid out while the menu is open"
    );
    click_row(app, probe, wave);
    assert_eq!(
        probe.wave, WAVE_VALUE,
        "clicking the radio item set its &mut i32 to the option value"
    );
    assert!(
        probe.menu_open,
        "the radio item keeps the menu open by default"
    );
}

fn open_submenu_and_click_child(app: &mut Application, probe: &mut MenuProbe) {
    let sub = probe
        .sub_row
        .expect("frame populates the Sub row state every tick");
    assert!(
        sub.found,
        "the submenu parent row should be laid out while the menu is open"
    );
    click_row(app, probe, sub);
    assert!(
        app.tick(probe, frame),
        "settle so the submenu child lays out beside the parent"
    );

    let child = probe
        .child_row
        .expect("frame populates the Child row state every tick");
    assert!(
        child.found && child.size.x > 0.0,
        "the submenu child should be laid out while the submenu is open"
    );
    click_row(app, probe, child);
    assert!(
        probe.child_clicked,
        "menu_item!(\"Child\").clicked should fire on a click over it"
    );
}

#[test]
fn headless_menu_check_radio_and_submenu() {
    let mut app = Application::new(WIN_W, WIN_H, true);
    let mut probe = MenuProbe::default();

    assert!(app.tick(&mut probe, frame), "initial layout tick");
    common::right_click_at(&mut app, &mut probe, frame, OPEN_X, OPEN_Y);
    assert!(probe.menu_open, "right-click in the region opened the menu");

    assert!(
        app.tick(&mut probe, frame),
        "settle so the menu items lay out"
    );
    toggle_check_item(&mut app, &mut probe);
    select_radio_item(&mut app, &mut probe);
    open_submenu_and_click_child(&mut app, &mut probe);
}
