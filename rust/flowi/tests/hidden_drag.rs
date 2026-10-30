use flowi::{Application, UiInput, Vec2};

// Exact in f32 so the scaled read (12.0 * 0.5 = 6.0, -8.0 * 0.5 = -4.0) compares with no tolerance.
const PRESS_X: i32 = 50;
const PRESS_Y: i32 = 60;
const SCALE: f32 = 0.5;
const RAW_DX: f32 = 12.0;
const RAW_DY: f32 = -8.0;

#[derive(Clone, Copy)]
enum Phase {
    Begin,
    Read,
    End,
    Idle,
}

struct DragProbe {
    phase: Phase,
    delta: Vec2,
}

fn frame(p: &mut DragProbe) {
    match p.phase {
        Phase::Begin => UiInput::mouse_hidden_drag_begin(SCALE),
        Phase::Read => p.delta = UiInput::mouse_drag_delta(),
        Phase::End => UiInput::mouse_hidden_drag_end(),
        Phase::Idle => p.delta = UiInput::mouse_drag_delta(),
    }
}

#[test]
fn headless_hidden_drag() {
    let mut app = Application::new(120, 80, true);
    let mut probe = DragProbe {
        phase: Phase::Idle,
        delta: Vec2 { x: 0.0, y: 0.0 },
    };

    // The inject is applied on this tick's pump, so begin (next tick) sees it as the absolute position to
    // remember.
    app.inject_mouse_move(PRESS_X, PRESS_Y);
    assert!(app.tick(&mut probe, frame), "move tick should keep running");

    probe.phase = Phase::Begin;
    assert!(
        app.tick(&mut probe, frame),
        "begin tick should keep running"
    );
    assert!(
        app.headless_relative_mouse_mode(),
        "begin enters relative-mouse mode"
    );

    // The accumulator caches the raw delta this tick, so the frame's mouse_drag_delta returns it multiplied by
    // the begin scale.
    probe.phase = Phase::Read;
    app.inject_mouse_delta(RAW_DX, RAW_DY);
    assert!(app.tick(&mut probe, frame), "read tick should keep running");
    assert_eq!(
        probe.delta.x,
        RAW_DX * SCALE,
        "drag delta x is the raw delta scaled"
    );
    assert_eq!(
        probe.delta.y,
        RAW_DY * SCALE,
        "drag delta y is the raw delta scaled"
    );

    probe.phase = Phase::End;
    assert!(app.tick(&mut probe, frame), "end tick should keep running");
    assert!(
        !app.headless_relative_mouse_mode(),
        "end exits relative-mouse mode"
    );
    let warp = app.headless_last_warp();
    assert_eq!(
        warp.x, PRESS_X as f32,
        "end warps the cursor back to the press point x"
    );
    assert_eq!(
        warp.y, PRESS_Y as f32,
        "end warps the cursor back to the press point y"
    );

    probe.phase = Phase::Idle;
    assert!(app.tick(&mut probe, frame), "idle tick should keep running");
    assert_eq!(probe.delta.x, 0.0, "drag delta x clears after end");
    assert_eq!(probe.delta.y, 0.0, "drag delta y clears after end");
}
