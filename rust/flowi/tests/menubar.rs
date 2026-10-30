//! Tops and items lay out under the `(label, (level << 16) | index)` id the C core hashes, so the same
//! (name, seed) query finds their rects.

// The returns inside the raii helpers are the early exits under test.
#![allow(clippy::needless_return)]

use flowi::{
    element_state, menu, menu_item, menu_region, menubar, menubar_menu, submenu, Application,
    ElementState, MenuBar,
};

mod common;

// Large enough that the dropdown opens downward unclamped; ui_scale is 1.0, so window px equal logical px.
const WIN_W: u32 = 800;
const WIN_H: u32 = 600;
// The seed of a row at build level 1 (an open dropdown's first row): `(1 << 16) | 0`.
const LEVEL_1: i32 = 1 << 16;
// A right-click point inside the full-window context region, clear of the bar strip.
const RAII_REGION_X: i32 = 400;
const RAII_REGION_Y: i32 = 400;

#[derive(Default)]
struct BarProbe {
    /// MenuBar::is_open snapshot taken AFTER the bar block each frame.
    bar_open: bool,
    open_clicked: bool,
    /// The File top lays out at the strip's level (("File", 0)).
    file_top: Option<ElementState>,
    /// Populated only while the File dropdown is open. The dropdown is the menu core's level 1, so the item
    /// hashes under ("Open", LEVEL_1).
    open_item: Option<ElementState>,
    with_region: bool,
    menu_body_ran: bool,
    file_body_ran: bool,
}

// The strip's body runs every frame; a top's body runs only while its dropdown is open.
fn frame(p: &mut BarProbe) {
    menubar! { "app_bar" {
        menubar_menu! { "File" {
            if menu_item!("Open").clicked {
                p.open_clicked = true;
            }
        }}
        menubar_menu! { "Edit" {
            menu_item!("Undo");
        }}
    }}
    p.bar_open = MenuBar::is_open("app_bar");
    // Reports the previous frame's resolved layout.
    p.file_top = Some(element_state(("File", 0)));
    p.open_item = Some(element_state(("Open", LEVEL_1)));
}

// Early exits from menu bodies: the guards must still close each level, so the frame ends balanced.
fn menu_body_returns(p: &mut BarProbe) {
    menu! { "ctx_menu" {
        p.menu_body_ran = true;
        return;
    }}
}

fn dropdown_body_try(p: &mut BarProbe) -> Option<()> {
    menubar_menu! { "File" {
        p.file_body_ran = true;
        None::<()>?;
    }}
    Some(())
}

fn region_body_returns() {
    menu_region! { "ctx_region", "ctx_menu" {
        return;
    }}
}

fn raii_frame(p: &mut BarProbe) {
    if p.with_region {
        // Region phase: the region is a catch-all over the window, so it runs without the bar.
        region_body_returns();
        menu_body_returns(p);
        return;
    }
    // submenu! shares its expansion with menubar_menu!; its own begin never opens here.
    menu! { "closed" { submenu! { "Sub" { menu_item!("x"); }} }}
    menubar! { "app_bar" {
        let _ = dropdown_body_try(p);
    }}
    p.file_top = Some(element_state(("File", 0)));
    // Emitted after the early exits; a leaked level would trip the balance check at frame end.
    menu_item!("after");
}

fn panic_frame(_: &mut BarProbe) {
    // A panic out of a body unwinds through the guard.
    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        menubar! { "pbar" {
            panic!("boom");
        }}
    }));
    assert!(r.is_err());
}

#[test]
fn headless_menu_bar_click() {
    let mut app = Application::new(WIN_W, WIN_H, true);
    let mut probe = BarProbe::default();

    assert!(app.tick(&mut probe, frame), "initial layout tick");
    assert!(!probe.bar_open, "bar starts with no dropdown open");

    // element_state reads the previous frame's layout, so settle one more tick before pulling the rect.
    assert!(
        app.tick(&mut probe, frame),
        "settle so the strip tops lay out"
    );
    let file = probe
        .file_top
        .expect("frame populates the File top state every tick");
    assert!(
        file.found,
        "File top should be laid out in the always-run strip"
    );
    assert!(
        file.size.x > 0.0 && file.size.y > 0.0,
        "File top should have a non-empty rect"
    );
    let (fx, fy) = common::center(file);

    common::click_at(&mut app, &mut probe, frame, fx, fy);
    assert!(probe.bar_open, "clicking the File top opened its dropdown");

    assert!(
        app.tick(&mut probe, frame),
        "settle so the dropdown item lays out"
    );
    let open = probe
        .open_item
        .expect("frame populates the Open item state every tick");
    assert!(
        open.found,
        "Open item should be laid out while the File dropdown is open"
    );
    assert!(
        open.size.x > 0.0 && open.size.y > 0.0,
        "Open item should have a non-empty rect"
    );
    let (ox, oy) = common::center(open);

    common::click_at(&mut app, &mut probe, frame, ox, oy);
    assert!(
        probe.open_clicked,
        "menu_item!(\"Open\").clicked should fire on a click over it"
    );
    assert!(
        !probe.bar_open,
        "the default close policy closed the dropdown on activation"
    );

    // Open the context menu first (region phase, no bar) so its body runs and returns early, then dismiss it.
    probe.with_region = true;
    common::right_click_at(
        &mut app,
        &mut probe,
        raii_frame,
        RAII_REGION_X,
        RAII_REGION_Y,
    );
    assert!(
        probe.menu_body_ran,
        "context menu body ran and returned early"
    );
    common::click_at(&mut app, &mut probe, raii_frame, 5, 5);

    // Then open the File dropdown so its body runs and exits via `?`.
    probe.with_region = false;
    let file = probe.file_top.expect("frame populated File top");
    let (fx, fy) = common::center(file);
    common::click_at(&mut app, &mut probe, raii_frame, fx, fy);
    assert!(
        probe.file_body_ran,
        "File dropdown body ran and exited via ?"
    );
    // Another tick would abort in the C core had any early exit left a level open.
    assert!(
        app.tick(&mut probe, raii_frame),
        "raii frame stays balanced"
    );

    // A panic out of a body unwinds through the guard; the following tick would abort on an open level.
    assert!(app.tick(&mut probe, panic_frame), "panic frame");
    assert!(app.tick(&mut probe, raii_frame), "balanced after panic");
}
