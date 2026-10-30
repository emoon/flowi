//! A plain area never enters the interaction state on its own - only a widget's behavior or the
//! [`scroll_target`] opt-in registers one.
//!
//! ui_scale is 1.0, so logical px equal physical px here and the fixed size reads back exactly.

use flowi::{col, element_state, fixed, Application, UiInput};

mod common;

// The container, the scroll-target registration and the pulled read share one id.
const REGION_ID: &str = "es_region";
const ABSENT_ID: &str = "es_never_registered";
const W: f32 = 80.0;
const H: f32 = 40.0;

#[derive(Default)]
struct Probe {
    found: bool,
    visible: bool,
    size: (f32, f32),
    hot: bool,
    absent_found: bool,
}

// Single root child with default top-left alignment, so the region occupies (0,0)..(80,40).
fn frame(p: &mut Probe) {
    col! { REGION_ID, width: fixed!(W), height: fixed!(H), color: [40, 40, 60] {
        UiInput::scroll_target(REGION_ID);
    }}
    let s = element_state(REGION_ID);
    p.found = s.found;
    p.visible = s.visible;
    p.size = (s.size.x, s.size.y);
    p.hot = s.hot;
    p.absent_found = element_state(ABSENT_ID).found;
}

#[test]
fn headless_element_state_rect_and_hot() {
    let mut app = Application::new(200, 120, true);
    let mut probe = Probe::default();

    // (150,100) parks the cursor outside the region (x > 80). The rect is the previous frame's resolved
    // layout, so a few frames are needed before it is populated.
    app.inject_mouse_move(150, 100);
    common::tick_n(&mut app, &mut probe, frame, 4, "settle");

    assert!(probe.found, "a registered element reports found = true");
    assert!(probe.visible, "the on-screen region is visible");
    assert_eq!(
        probe.size,
        (W, H),
        "size is the logical authoring size (scale 1.0 => physical == logical)"
    );
    assert!(
        !probe.absent_found,
        "a never-registered id reports found = false"
    );
    assert!(!probe.hot, "cursor parked outside the region is not hot");

    app.inject_mouse_move(10, 10);
    common::tick_n(&mut app, &mut probe, frame, 3, "hover");
    assert!(probe.hot, "cursor over the region makes it hot");

    app.inject_mouse_move(150, 100);
    common::tick_n(&mut app, &mut probe, frame, 3, "un-hover");
    assert!(!probe.hot, "cursor leaving the region clears hot");
}
