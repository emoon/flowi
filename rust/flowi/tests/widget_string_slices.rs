//! Dropdown, TabBar and RadioGroup take `&[impl AsRef<str>]` option lists, using only the
//! flowi crate. The clamp and hit-test assertions do not depend on which bytes landed in
//! which slot, so the packed contents are checked by pixel comparison.

use flowi::{col, grow, Application, Dropdown, MouseButton, PixelView, RadioGroup, TabBar};

mod common;

// Tabs lay out left-to-right from the top-left of the window, so (10, 10) is inside the first tab; ui_scale is
// 1.0, so window px equal layout px.
const HIT_X: i32 = 10;
const HIT_Y: i32 = 10;

// Below the tab strip and clear of every widget the frames below draw, so nothing is hot while pixels are
// compared. Inside the 200x120 window, so the headless backend keeps it as given.
const RESTING_X: i32 = 190;
const RESTING_Y: i32 = 115;

const TABS: [&str; 3] = ["General", "Audio", "Video"];
const QUALITY: [&str; 3] = ["Low", "Medium", "High"];

// Out of range for their 3-element lists, so each is clamped to 0. tab starts in range so the press below has
// something to change.
const RADIO_START: i32 = -1;
const DROPDOWN_START: i32 = 5;
const TAB_START: i32 = 2;

struct Probe {
    tab: i32,
    radio: i32,
    dropdown: i32,
    dropdown_open: bool,
    tab_changed: bool,
    radio_len: usize,
    lone_tab: i32,
}

impl Default for Probe {
    fn default() -> Self {
        Probe {
            tab: TAB_START,
            radio: RADIO_START,
            dropdown: DROPDOWN_START,
            dropdown_open: false,
            tab_changed: false,
            radio_len: 0,
            lone_tab: 7,
        }
    }
}

fn frame(p: &mut Probe) {
    col! { width: grow!(), height: grow!() {
        // Borrowed &'static str literals - the common case.
        p.tab_changed = TabBar::new(&TABS, &mut p.tab, Default::default());

        // Owned Strings dropped at the end of the statement, in a runtime-length slice.
        let options: Vec<String> = (0..3).map(|i| format!("Option {i}")).collect();
        p.radio_len = options.len();
        RadioGroup::new(&options, &mut p.radio, Default::default());

        // Kept closed.
        Dropdown::new("Quality", &mut p.dropdown_open, &QUALITY, &mut p.dropdown, Default::default());

        // A one-element list.
        TabBar::new(&["Only"], &mut p.lone_tab, Default::default());
    }}
}

#[test]
fn headless_widget_string_slices() {
    let mut app = Application::new(200, 120, true);
    // A fixed 0.1s step settles every eased widget state within the frame budget, so the byte-exact pixel
    // comparisons below see settled colors.
    app.set_test_dt(0.1);
    let mut probe = Probe::default();

    // Layout geometry is the previous frame's, so the hit-test needs a few frames before a press can
    // land.
    app.inject_mouse_move(HIT_X, HIT_Y);
    common::tick_n(&mut app, &mut probe, frame, 4, "settle");

    assert_eq!(
        probe.radio_len, 3,
        "the runtime-length options list reached the widget"
    );
    assert_eq!(
        probe.radio, 0,
        "radio group clamps an out-of-range index ({RADIO_START}) against the option count it was handed"
    );
    assert_eq!(
        probe.dropdown, 0,
        "dropdown clamps an out-of-range index ({DROPDOWN_START}) against the option count it was handed"
    );
    assert_eq!(probe.tab, TAB_START, "an in-range tab index is left alone");
    assert_eq!(
        probe.lone_tab, 0,
        "a one-element list clamps an out-of-range index to its only slot"
    );
    assert!(
        !probe.tab_changed,
        "no selection change before any input is injected"
    );

    // The tab strip selects on press (not release), so the press tick alone moves it.
    app.inject_mouse_button(MouseButton::Left, true);
    assert!(
        app.tick(&mut probe, frame),
        "press tick should keep running"
    );
    app.inject_mouse_button(MouseButton::Left, false);
    common::tick_n(&mut app, &mut probe, frame, 3, "observe");

    assert_eq!(
        probe.tab, 0,
        "pressing the first tab selects it — the labels laid out into real hit-testable geometry, so the \
         arena-packed FlString array pointed at the right bytes"
    );

    // Park the cursor off the tab strip so no render below carries hover state into the byte-exact
    // comparisons.
    app.inject_mouse_move(RESTING_X, RESTING_Y);

    // The comparisons below each render two option lists through the same widget. Labels are chosen to have
    // obviously different drawn widths so a difference is unmissable at 200x120.
    let distinct = render_tabs(&mut app, &["WWWWWWWW", "i", "MMMM"]);
    let repeated = render_tabs(&mut app, &["WWWWWWWW", "WWWWWWWW", "WWWWWWWW"]);
    assert_ne!(
        distinct, repeated,
        "three distinct tab labels must not render identically to the same label repeated — if they do, the \
         packing loop wrote one element into every slot"
    );

    let forward = render_tabs(&mut app, &["WWWWWWWW", "i"]);
    let reversed = render_tabs(&mut app, &["i", "WWWWWWWW"]);
    assert_ne!(
        forward, reversed,
        "a label list must not render identically to its reverse — if it does, slot order is being ignored"
    );

    // Anchors slot 0 to an absolute reference: the tab strip is left-aligned with fit-sized children, so tab 0
    // starts at x = 0 and a two-element list paints its leftmost columns like the one-element list with the
    // same first label. 20 columns is inside the wide label's own tab.
    const ANCHOR_COLS: u32 = 20;
    let two = render_tabs_view(&mut app, &["WWWWWWWW", "i"]);
    let one = render_tabs_view(&mut app, &["WWWWWWWW"]);
    assert_eq!(
        left_columns(&two, ANCHOR_COLS),
        left_columns(&one, ANCHOR_COLS),
        "slot 0 must hold the FIRST label: the leftmost {ANCHOR_COLS} columns of a two-element list should \
         match the one-element list with the same leading label, but they diverged — the array is not being \
         packed in caller order"
    );

    // The dropdown's trigger draws "label: options[selected_index]", so the only difference between the two
    // frames is which slot the widget reads.
    let pick_first = render_dropdown(&mut app, &QUALITY, 0);
    let pick_second = render_dropdown(&mut app, &QUALITY, 1);
    assert_ne!(
        pick_first, pick_second,
        "selecting index 0 vs 1 must change the dropdown's trigger text — if it does not, index i is not \
         reading slot i of the packed array"
    );
}

/// The leftmost cols columns of every row, as a flat byte vector.
fn left_columns(view: &PixelView, cols: u32) -> Vec<u8> {
    let cols = cols.min(view.width) as usize;
    let row = view.width as usize * view.channels;
    (0..view.height as usize)
        .flat_map(|y| view.pixels[y * row..y * row + cols * view.channels].to_vec())
        .collect()
}

fn render_tabs_view(app: &mut Application, labels: &[&str]) -> PixelView {
    let mut selected = 0i32;
    // Sizing settles on the second tick; the extra four let the eased widget state reach its snap target.
    for _ in 0..6 {
        assert!(
            app.tick(&mut selected, |sel: &mut i32| {
                col! { width: grow!(), height: grow!() {
                    TabBar::new(labels, sel, Default::default());
                }}
            }),
            "tab render tick should keep running"
        );
    }
    app.read_pixels().expect("render backend readback")
}

fn render_tabs(app: &mut Application, labels: &[&str]) -> Vec<u8> {
    let mut selected = 0i32;
    // Sizing settles on the second tick; the extra four let the eased widget state reach its snap target.
    for _ in 0..6 {
        assert!(
            app.tick(&mut selected, |sel: &mut i32| {
                col! { width: grow!(), height: grow!() {
                    TabBar::new(labels, sel, Default::default());
                }}
            }),
            "tab render tick should keep running"
        );
    }
    app.read_pixels().expect("render backend readback").pixels
}

/// Renders the dropdown closed, with selected picked.
fn render_dropdown(app: &mut Application, options: &[&str], selected: i32) -> Vec<u8> {
    let mut state = (false, selected);
    for _ in 0..6 {
        assert!(
            app.tick(&mut state, |s: &mut (bool, i32)| {
                col! { width: grow!(), height: grow!() {
                    Dropdown::new("Q", &mut s.0, options, &mut s.1, Default::default());
                }}
            }),
            "dropdown render tick should keep running"
        );
    }
    app.read_pixels().expect("render backend readback").pixels
}
