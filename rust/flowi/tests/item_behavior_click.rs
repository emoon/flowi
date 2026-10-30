use flowi::{col, fixed, Application, Item, ItemFlags, MouseButton};

mod common;

// A single root child with default top-left alignment occupies (0,0)..(60,40); ui_scale is 1.0, so window px
// equal layout px and (10, 10) is inside.
const REGION_ID: &str = "ib_region";
const HIT_X: i32 = 10;
const HIT_Y: i32 = 10;
const W: f32 = 60.0;
const H: f32 = 40.0;

#[derive(Default)]
struct ClickProbe {
    armed: bool,
    hovered: bool,
    clicked_while_idle: bool,
    clicked_while_armed: bool,
}

// The element has no widget visuals; item_behavior over its id makes it interactive.
fn frame(p: &mut ClickProbe) {
    col! { REGION_ID, width: fixed!(W), height: fixed!(H), color: [40, 40, 60] {
        let sig = Item::behavior(
            REGION_ID,
            ItemFlags::CLICKABLE | ItemFlags::DRAGGABLE | ItemFlags::DOUBLE_CLICK | ItemFlags::RIGHT_CLICK,
        );
        p.hovered = sig.hovered;
        if p.armed {
            p.clicked_while_armed |= sig.clicked;
        } else {
            p.clicked_while_idle |= sig.clicked;
        }
    }}
}

#[test]
fn headless_item_behavior_click() {
    let mut app = Application::new(200, 120, true);
    let mut probe = ClickProbe::default();

    app.inject_mouse_move(HIT_X, HIT_Y);
    common::tick_n(&mut app, &mut probe, frame, 4, "settle");
    assert!(
        !probe.clicked_while_idle,
        "no click should register before any button event is injected"
    );
    assert!(
        probe.hovered,
        "cursor over ({HIT_X},{HIT_Y}) should be hovering the region after settle"
    );

    // Injected input is applied one tick later and click detection trails the release by a frame, so observe
    // several frames after the release.
    probe.armed = true;
    app.inject_mouse_button(MouseButton::Left, true);
    assert!(
        app.tick(&mut probe, frame),
        "press tick should keep running"
    );
    app.inject_mouse_button(MouseButton::Left, false);
    assert!(
        app.tick(&mut probe, frame),
        "release tick should keep running"
    );
    common::tick_n(&mut app, &mut probe, frame, 4, "observe");

    assert!(
        probe.clicked_while_armed,
        "item_behavior(...).clicked should fire after a press+release over the element"
    );
}
