use flowi::{col, fixed, Application, Item, ItemFlags, MouseButton, UiInput};

mod common;

const KNOB_ID: &str = "knob";
// The knob is the root's single child at the top-left, so it occupies (0,0)..(SIZE,SIZE); ui_scale is 1.0, so
// layout px equal injected window px.
const SIZE: f32 = 72.0;
const CENTER: i32 = 36; // SIZE / 2

// Powers of two, so the scrub arithmetic is exact in f32: RAW_DY * DRAG_SCALE = -2.0 and the value step is
// -(-2.0) * SENSITIVITY = 0.0625.
const DRAG_SCALE: f32 = 0.5;
const SENSITIVITY: f32 = 1.0 / 32.0;
const RAW_DY: f32 = -4.0;
const START_VALUE: f32 = 0.5;

struct Knob {
    value: f32,
    /// Previous-frame grab latch, so begin/end fire on the drag's edges.
    dragging: bool,
    hovered: bool,
}

fn frame(k: &mut Knob) {
    col! { KNOB_ID, width: fixed!(SIZE), height: fixed!(SIZE), color: [40, 40, 60] {
        let sig = Item::behavior(KNOB_ID, ItemFlags::CLICKABLE | ItemFlags::DRAGGABLE);
        k.hovered = sig.hovered;

        if sig.dragging && !k.dragging {
            UiInput::mouse_hidden_drag_begin(DRAG_SCALE);
        } else if !sig.dragging && k.dragging {
            UiInput::mouse_hidden_drag_end();
        }
        k.dragging = sig.dragging;

        if sig.dragging {
            // Drag up (negative screen-y) raises the value; the delta is already scaled by DRAG_SCALE.
            let dy = UiInput::mouse_drag_delta().y;
            k.value = (k.value - dy * SENSITIVITY).clamp(0.0, 1.0);
        }
    }}
}

#[test]
fn headless_knob_scrub_moves_value() {
    let mut app = Application::new(200, 120, true);
    let mut knob = Knob {
        value: START_VALUE,
        dragging: false,
        hovered: false,
    };

    app.inject_mouse_move(CENTER, CENTER);
    common::tick_n(&mut app, &mut knob, frame, 4, "settle");
    assert!(
        knob.hovered,
        "cursor over the knob centre should be hovering after settle"
    );
    assert_eq!(
        knob.value, START_VALUE,
        "value must not move before the grab"
    );

    // Tick twice so the grab latches and begin flushes into relative-mouse mode.
    app.inject_mouse_button(MouseButton::Left, true);
    assert!(app.tick(&mut knob, frame), "press tick should keep running");
    assert!(
        app.tick(&mut knob, frame),
        "grab-settle tick should keep running"
    );
    assert!(
        app.headless_relative_mouse_mode(),
        "grabbing the knob should begin a hidden relative drag (relative-mouse mode)"
    );
    assert_eq!(
        knob.value, START_VALUE,
        "value must not move until a delta is injected"
    );

    // The accumulator caches the raw delta this tick, so the frame reads it back scaled.
    app.inject_mouse_delta(0.0, RAW_DY);
    assert!(app.tick(&mut knob, frame), "scrub tick should keep running");

    app.inject_mouse_button(MouseButton::Left, false);
    assert!(
        app.tick(&mut knob, frame),
        "release tick should keep running"
    );

    let scaled = RAW_DY * DRAG_SCALE; // -2.0, exact
    let expected = START_VALUE - scaled * SENSITIVITY; // 0.5625, exact
    assert!(
        knob.value > START_VALUE,
        "dragging up should raise the value (was {START_VALUE}, now {})",
        knob.value
    );
    assert_eq!(
        knob.value, expected,
        "the value should move by the exact scaled delta"
    );
}
