//! An embedded host renders, reads back and pushes input with no unsafe code and no flowi_sys import.

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

#[test]
fn embedded_renders_reads_back_and_takes_input() {
    let mut app =
        Application::embedded(EmbeddedSettings::new(WIDTH, HEIGHT)).expect("embedded flowi");

    app.tick_embedded(1.0 / 60.0, || {});

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

    for _ in 0..3 {
        let state = app.tick_embedded(1.0 / 60.0, || mouse_button(MouseButton::Left));
        assert!(
            !state.pressed && !state.held && !state.released,
            "no button channel should report before any event is pushed"
        );
    }

    // The event is drained by the pump on the next frame boundary; the pressed edge lasts one
    // frame, the held level is sticky.
    app.push_input(button_event(MouseEventType::Button_Down, MouseButton::Left));
    let mut pressed_seen = false;
    let mut held_seen = false;
    for _ in 0..3 {
        let state = app.tick_embedded(1.0 / 60.0, || mouse_button(MouseButton::Left));
        pressed_seen |= state.pressed;
        held_seen |= state.held;
    }
    assert!(
        pressed_seen,
        "a pushed Button_Down should raise the pressed edge"
    );
    assert!(held_seen, "a pushed Button_Down should hold the level");

    app.push_input(button_event(MouseEventType::Button_Up, MouseButton::Left));
    let mut released_seen = false;
    let mut last_held = true;
    for _ in 0..3 {
        let state = app.tick_embedded(1.0 / 60.0, || mouse_button(MouseButton::Left));
        released_seen |= state.released;
        last_held = state.held;
    }
    assert!(
        released_seen,
        "a pushed Button_Up should raise the released edge"
    );
    assert!(!last_held, "the held level should drop once released");
}
