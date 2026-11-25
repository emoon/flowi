//! Every mouse, gamepad and window event an embedded host pushes in is a safe enum, built here without
//! `unsafe`, and each one that has an effect a frame can read shows it.

#![forbid(unsafe_code)]

use flowi::{
    Application, EmbeddedSettings, GamepadAxis, GamepadAxisData, GamepadButton, GamepadButtonData,
    GamepadEventKind, InputEvent, MouseButton, MouseButtonData, MouseEventKind, MouseMoveData,
    MouseRelativeData, MouseWheelData, SafeGamepadEvent, SafeMouseEvent, SafeWindowEvent, UiInput,
    WindowEventKind, WindowResizeData,
};

const WIDTH: u32 = 64;
const HEIGHT: u32 = 48;

fn app() -> Application {
    Application::embedded(EmbeddedSettings::new(WIDTH, HEIGHT)).expect("embedded flowi")
}

/// One frame: begin, `ui`, end.
fn frame<R>(app: &mut Application, ui: impl FnOnce() -> R) -> R {
    app.begin_frame(1.0 / 60.0);
    let result = ui();
    app.end_frame();
    result
}

fn mouse(kind: MouseEventKind) -> InputEvent<'static> {
    InputEvent::Mouse(SafeMouseEvent { kind, timestamp: 1 })
}

fn gamepad(gamepad_id: u32, kind: GamepadEventKind) -> InputEvent<'static> {
    InputEvent::Gamepad(SafeGamepadEvent {
        kind,
        gamepad_id,
        timestamp: 1,
    })
}

fn window(kind: WindowEventKind<'_>) -> InputEvent<'_> {
    InputEvent::Window(SafeWindowEvent { kind, timestamp: 1 })
}

fn move_to(x: f32, y: f32) -> InputEvent<'static> {
    mouse(MouseEventKind::Move(MouseMoveData {
        x,
        y,
        delta_x: 0.0,
        delta_y: 0.0,
    }))
}

fn a_mouse_move_shows_as_the_frames_pointer_delta() {
    let mut app = app();
    app.push_input(move_to(10.0, 10.0));
    frame(&mut app, || {});

    app.push_input(move_to(14.0, 13.0));
    let delta = frame(&mut app, UiInput::mouse_delta);
    assert_eq!(
        (delta.x, delta.y),
        (4.0, 3.0),
        "the delta is how far the pointer moved since the frame before"
    );
}

fn a_mouse_button_pair_shows_as_the_press_and_release_edges() {
    let mut app = app();
    let at = |button| MouseButtonData {
        button,
        x: 5.0,
        y: 6.0,
        click_count: 1,
    };

    app.push_input(mouse(MouseEventKind::ButtonDown(at(MouseButton::Right))));
    let pressed = frame(&mut app, || UiInput::mouse_pressed(MouseButton::Right));
    assert!(
        pressed,
        "a pushed ButtonDown presses the button in the next frame"
    );
    assert!(
        !frame(&mut app, || UiInput::mouse_pressed(MouseButton::Left)),
        "the other buttons stay up"
    );

    app.push_input(mouse(MouseEventKind::ButtonUp(at(MouseButton::Right))));
    let released = frame(&mut app, || UiInput::mouse_released(MouseButton::Right));
    assert!(released, "a pushed ButtonUp releases it in the next frame");
}

fn a_mouse_wheel_shows_as_the_frames_wheel_delta() {
    let mut app = app();
    app.push_input(mouse(MouseEventKind::Wheel(MouseWheelData {
        x: 0.0,
        y: 3.0,
    })));
    let wheel = frame(&mut app, UiInput::mouse_wheel);
    assert_eq!(
        (wheel.x, wheel.y),
        (0.0, 3.0),
        "the wheel delta of the event"
    );

    let wheel = frame(&mut app, UiInput::mouse_wheel);
    assert_eq!(
        (wheel.x, wheel.y),
        (0.0, 0.0),
        "a wheel tick belongs to one frame"
    );
}

fn a_relative_motion_during_a_hidden_drag_shows_as_the_drag_delta() {
    let mut app = app();
    app.push_input(move_to(20.0, 20.0));
    frame(&mut app, || {});
    frame(&mut app, || UiInput::mouse_hidden_drag_begin(1.0));

    app.push_input(mouse(MouseEventKind::RelativeMotion(MouseRelativeData {
        delta_x: 6.0,
        delta_y: -2.0,
    })));
    let delta = frame(&mut app, UiInput::mouse_drag_delta);
    assert_eq!(
        (delta.x, delta.y),
        (6.0, -2.0),
        "the raw motion of the hidden drag"
    );
    frame(&mut app, UiInput::mouse_hidden_drag_end);
}

fn a_gamepad_connecting_pressing_and_leaving_shows_in_its_slot() {
    let mut app = app();
    assert!(!frame(&mut app, || UiInput::gamepad_connected(0)));

    app.push_input(gamepad(0, GamepadEventKind::Connected));
    assert!(
        frame(&mut app, || UiInput::gamepad_connected(0)),
        "a Connected event connects the slot of its gamepad id"
    );
    assert!(
        !frame(&mut app, || UiInput::gamepad_connected(1)),
        "and no other slot"
    );

    app.push_input(gamepad(
        0,
        GamepadEventKind::ButtonDown(GamepadButtonData {
            button: GamepadButton::A,
        }),
    ));
    let (pressed, down, other) = frame(&mut app, || {
        (
            UiInput::gamepad_button_pressed(0, GamepadButton::A),
            UiInput::gamepad_button_down(0, GamepadButton::A),
            UiInput::gamepad_button_down(0, GamepadButton::B),
        )
    });
    assert!(
        pressed && down && !other,
        "ButtonDown presses only its button"
    );

    app.push_input(gamepad(
        0,
        GamepadEventKind::ButtonUp(GamepadButtonData {
            button: GamepadButton::A,
        }),
    ));
    let (released, down) = frame(&mut app, || {
        (
            UiInput::gamepad_button_released(0, GamepadButton::A),
            UiInput::gamepad_button_down(0, GamepadButton::A),
        )
    });
    assert!(released && !down, "ButtonUp releases it");

    app.push_input(gamepad(0, GamepadEventKind::Disconnected));
    assert!(
        !frame(&mut app, || UiInput::gamepad_connected(0)),
        "a Disconnected event disconnects the slot"
    );
}

fn a_gamepad_axis_motion_shows_as_the_axis_value() {
    let mut app = app();
    app.push_input(gamepad(0, GamepadEventKind::Connected));
    app.push_input(gamepad(
        0,
        GamepadEventKind::AxisMotion(GamepadAxisData {
            axis: GamepadAxis::Right_Trigger,
            value: 0.75,
        }),
    ));
    let (trigger, stick) = frame(&mut app, || {
        (
            UiInput::gamepad_axis(0, GamepadAxis::Right_Trigger),
            UiInput::gamepad_axis(0, GamepadAxis::Left_X),
        )
    });
    assert_eq!(trigger, 0.75, "the axis the event names holds its value");
    assert_eq!(stick, 0.0, "the other axes stay at rest");
}

fn a_dropped_file_shows_with_its_path_and_release_point() {
    let mut app = app();
    // The path is borrowed for the push only; the frame reads the copy flowi made.
    let path = String::from("/tmp/kick.wav");
    app.push_input(window(WindowEventKind::DropFile {
        path: &path,
        x: 12.0,
        y: 8.0,
    }));
    drop(path);
    let (count, path, at) = frame(&mut app, || {
        (
            UiInput::dropped_file_count(),
            UiInput::dropped_file(0),
            UiInput::dropped_file_position(0),
        )
    });
    assert_eq!(count, 1, "one file dropped");
    assert_eq!(path, "/tmp/kick.wav");
    assert_eq!((at.x, at.y), (12.0, 8.0));
    assert_eq!(
        frame(&mut app, UiInput::dropped_file_count),
        0,
        "a drop belongs to one frame"
    );
}

/// The events that only set flags no query reads change none of the state the queries do read.
fn the_events_without_a_readable_effect_leave_the_input_state_alone() {
    let mut app = app();
    app.push_input(mouse(MouseEventKind::Enter));
    app.push_input(mouse(MouseEventKind::Leave));
    app.push_input(window(WindowEventKind::None));
    app.push_input(window(WindowEventKind::FocusGained));
    app.push_input(window(WindowEventKind::FocusLost));
    app.push_input(window(WindowEventKind::Resize(WindowResizeData {
        width: WIDTH,
        height: HEIGHT,
    })));
    app.push_input(window(WindowEventKind::Close));
    let (pressed, connected, dropped) = frame(&mut app, || {
        (
            UiInput::mouse_pressed(MouseButton::Left) || UiInput::mouse_down(MouseButton::Left),
            UiInput::gamepad_connected(0),
            UiInput::dropped_file_count(),
        )
    });
    assert!(!pressed, "no button state changed");
    assert!(!connected, "no gamepad connected");
    assert_eq!(dropped, 0, "no file dropped");
}

const MOUSE_BUTTONS: [MouseButton; 5] = [
    MouseButton::Left,
    MouseButton::Right,
    MouseButton::Middle,
    MouseButton::X1,
    MouseButton::X2,
];

const PAD_BUTTONS: [GamepadButton; 15] = [
    GamepadButton::A,
    GamepadButton::B,
    GamepadButton::X,
    GamepadButton::Y,
    GamepadButton::Left_Bumper,
    GamepadButton::Right_Bumper,
    GamepadButton::Back,
    GamepadButton::Start,
    GamepadButton::Guide,
    GamepadButton::Left_Thumb,
    GamepadButton::Right_Thumb,
    GamepadButton::Dpad_Up,
    GamepadButton::Dpad_Right,
    GamepadButton::Dpad_Down,
    GamepadButton::Dpad_Left,
];

/// A button or axis that is the Count sentinel, and a gamepad id past the slots, are safe to build. The
/// toolkit drops them, so no state changes and no slot is indexed out of range.
fn a_sentinel_value_or_a_stray_gamepad_id_is_dropped_by_the_toolkit() {
    let mut app = app();
    app.push_input(mouse(MouseEventKind::ButtonDown(MouseButtonData {
        button: MouseButton::Count,
        x: 1.0,
        y: 1.0,
        click_count: 1,
    })));
    for stray_id in [u32::MAX, 1000] {
        app.push_input(gamepad(stray_id, GamepadEventKind::Connected));
        app.push_input(gamepad(
            stray_id,
            GamepadEventKind::ButtonDown(GamepadButtonData {
                button: GamepadButton::A,
            }),
        ));
        app.push_input(gamepad(
            stray_id,
            GamepadEventKind::AxisMotion(GamepadAxisData {
                axis: GamepadAxis::Left_X,
                value: 1.0,
            }),
        ));
    }
    app.push_input(gamepad(0, GamepadEventKind::Connected));
    app.push_input(gamepad(
        0,
        GamepadEventKind::ButtonDown(GamepadButtonData {
            button: GamepadButton::Count,
        }),
    ));
    app.push_input(gamepad(
        0,
        GamepadEventKind::AxisMotion(GamepadAxisData {
            axis: GamepadAxis::Count,
            value: 1.0,
        }),
    ));
    let (any_mouse, any_pad_button, any_axis, stray_slot) = frame(&mut app, || {
        (
            MOUSE_BUTTONS.into_iter().any(UiInput::mouse_down),
            PAD_BUTTONS
                .into_iter()
                .any(|b| UiInput::gamepad_button_down(0, b)),
            UiInput::gamepad_axis(0, GamepadAxis::Left_X) != 0.0,
            UiInput::gamepad_connected(1000),
        )
    });
    assert!(!any_mouse, "the Count button pressed nothing");
    assert!(!any_pad_button, "the Count pad button pressed nothing");
    assert!(!any_axis, "the stray ids and the Count axis moved nothing");
    assert!(!stray_slot, "a slot past the end reads as disconnected");
}

/// One Application at a time lives per process, so the cases run in turn from one test.
#[test]
fn an_embedded_host_pushes_every_safe_input_event() {
    a_mouse_move_shows_as_the_frames_pointer_delta();
    a_mouse_button_pair_shows_as_the_press_and_release_edges();
    a_mouse_wheel_shows_as_the_frames_wheel_delta();
    a_relative_motion_during_a_hidden_drag_shows_as_the_drag_delta();
    a_gamepad_connecting_pressing_and_leaving_shows_in_its_slot();
    a_gamepad_axis_motion_shows_as_the_axis_value();
    a_dropped_file_shows_with_its_path_and_release_point();
    the_events_without_a_readable_effect_leave_the_input_state_alone();
    a_sentinel_value_or_a_stray_gamepad_id_is_dropped_by_the_toolkit();
}
