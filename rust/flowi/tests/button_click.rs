use flowi::{col, grow, row, Application, Button, MouseButton};

mod common;

// The button sits at the layout origin; ui_scale is 1.0, so (10, 10) in window px is inside it.
const HIT_X: i32 = 10;
const HIT_Y: i32 = 10;

#[derive(Default)]
struct ClickProbe {
    armed: bool,
    hovered: bool,
    clicked_while_idle: bool,
    clicked_while_armed: bool,
}

fn frame(p: &mut ClickProbe) {
    col! { width: grow!(), height: grow!() {
        row! { {
            let sig = Button::new("Click me", Default::default());
            p.hovered = sig.hovered;
            if p.armed {
                p.clicked_while_armed |= sig.clicked;
            } else {
                p.clicked_while_idle |= sig.clicked;
            }
        } }
    }}
}

#[test]
fn headless_button_click() {
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
        "cursor over ({HIT_X},{HIT_Y}) should be hovering the button after settle"
    );

    // Injected input is applied one tick later and click detection trails the release by a frame, so
    // observe several frames after the release.
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
        "Button::new(\"Click me\").clicked should fire after a press+release over the button"
    );
}
