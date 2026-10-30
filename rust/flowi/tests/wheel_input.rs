use flowi::{col, grow, Application, UiInput};

// The container, the scroll-target flag and the scoped read share one id. The region grows to fill the
// window, so the cursor is always inside it.
const REGION_ID: &str = "wheel_region";
const HIT_X: i32 = 100;
const HIT_Y: i32 = 60;
// A downward wheel tick. ui_scale is 1.0 and no Shift is held, so the injected y survives the
// input pump unscaled and unswapped into the scoped read.
const WHEEL_Y: f32 = 120.0;

#[derive(Default)]
struct WheelProbe {
    last: (f32, f32),
}

// The region carries a background color; a colorless container is not hit-tested and never goes hot.
fn frame(p: &mut WheelProbe) {
    col! { REGION_ID, width: grow!(), height: grow!(), color: [40, 40, 60] {
        UiInput::scroll_target(REGION_ID);
        let d = UiInput::mouse_wheel_delta(REGION_ID);
        p.last = (d.x, d.y);
    }}
}

#[test]
fn headless_scroll_target_wheel_delta() {
    let mut app = Application::new(200, 120, true);
    let mut probe = WheelProbe::default();

    app.inject_mouse_move(HIT_X, HIT_Y);
    for _ in 0..4 {
        assert!(
            app.tick(&mut probe, frame),
            "settle tick should keep running"
        );
        assert_eq!(
            probe.last,
            (0.0, 0.0),
            "a Scroll Target with no wheel injected must read a zero delta"
        );
    }

    // The wheel is captured by the next tick's input pump and cached for that frame's scoped read; the region
    // is already hot, so consume-takes-all routes the full delta to it.
    app.inject_wheel(0.0, WHEEL_Y);
    assert!(
        app.tick(&mut probe, frame),
        "wheel tick should keep running"
    );
    assert_eq!(
        probe.last,
        (0.0, WHEEL_Y),
        "the hot Scroll Target must read the injected wheel delta for that frame"
    );

    assert!(
        app.tick(&mut probe, frame),
        "post-wheel tick should keep running"
    );
    assert_eq!(
        probe.last,
        (0.0, 0.0),
        "the wheel delta is per-frame: it resets to zero once no wheel is injected"
    );
}
