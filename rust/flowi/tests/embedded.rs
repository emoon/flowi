//! An embedded host renders, reads back and pushes input through the safe public API alone.

#![forbid(unsafe_code)]

use flowi::{
    mouse_button, Application, EmbeddedSettings, InputEvent, MouseButton, MouseButtonData,
    MouseEvent, MouseEventPayload, MouseEventType,
};

/// Default clear color (theme surface.base).
const CLEAR: [u8; 3] = [14, 14, 16];

const WIDTH: u32 = 64;
const HEIGHT: u32 = 48;

/// The button query is global and unrouted, so the exact cursor spot is irrelevant.
const CURSOR: (f32, f32) = (12.0, 20.0);

/// A decoded mouse button transition: the type tag plus the union arm it selects.
fn button_event(kind: MouseEventType, button: MouseButton) -> InputEvent {
    InputEvent::Mouse(MouseEvent {
        r#type: kind,
        payload: MouseEventPayload {
            button: MouseButtonData {
                button,
                x: CURSOR.0,
                y: CURSOR.1,
                click_count: 1,
            },
        },
        timestamp: 0,
    })
}

/// One frame: begin, `ui`, end.
fn frame<R>(app: &mut Application, ui: impl FnOnce() -> R) -> R {
    app.begin_frame(1.0 / 60.0);
    let result = ui();
    app.end_frame();
    result
}

/// An empty frame reads back as the clear colour at the size asked for.
fn assert_renders_and_reads_back(app: &mut Application) {
    frame(app, || {});

    let pixels = app.pixels().expect("software backend reads back");
    assert_eq!(pixels.width(), WIDTH, "readback width");
    assert_eq!(pixels.height(), HEIGHT, "readback height");
    assert_eq!(
        pixels.stride(),
        WIDTH * 3,
        "software backend reads back tightly-packed RGB24"
    );
    assert_eq!(
        pixels.rows().count(),
        HEIGHT as usize,
        "one row per scanline"
    );
    for (y, row) in pixels.rows().enumerate() {
        for (x, px) in row.as_chunks::<3>().0.iter().enumerate() {
            assert_eq!(
                px, &CLEAR,
                "pixel ({x},{y}) is {px:?}, expected clear color {CLEAR:?}"
            );
        }
    }
}

/// An event pushed before a frame is that frame's input: an edge shows in the very next
/// frame and lasts only that one, the held level is sticky.
fn assert_pushed_input_is_the_next_frames(app: &mut Application) {
    for _ in 0..3 {
        let state = frame(app, || mouse_button(MouseButton::Left));
        assert!(
            !state.pressed && !state.held && !state.released,
            "no button channel should report before any event is pushed"
        );
    }

    app.push_input(button_event(MouseEventType::Button_Down, MouseButton::Left));
    let state = frame(app, || mouse_button(MouseButton::Left));
    assert!(
        state.pressed && state.held,
        "a pushed Button_Down should be pressed and held in the next frame"
    );
    let state = frame(app, || mouse_button(MouseButton::Left));
    assert!(
        !state.pressed && state.held,
        "the pressed edge lasts one frame, the held level stays"
    );

    app.push_input(button_event(MouseEventType::Button_Up, MouseButton::Left));
    let state = frame(app, || mouse_button(MouseButton::Left));
    assert!(
        state.released && !state.held,
        "a pushed Button_Up should be released, and no longer held, in the next frame"
    );
    let state = frame(app, || mouse_button(MouseButton::Left));
    assert!(!state.released, "the released edge lasts one frame");
}

#[test]
fn embedded_renders_reads_back_and_takes_input() {
    let mut app =
        Application::embedded(EmbeddedSettings::new(WIDTH, HEIGHT)).expect("embedded flowi");

    assert_renders_and_reads_back(&mut app);
    assert_pushed_input_is_the_next_frames(&mut app);
}
