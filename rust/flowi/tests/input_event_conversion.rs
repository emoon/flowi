//! The safe events convert into the C structs and back, and a C struct holding a tag or enum value no
//! variant has is refused instead of read.
//!
//! Reading a C struct is the one place that takes `unsafe`: `try_from_raw` takes a pointer to bytes C
//! wrote. Stray values are written as plain integers, as C would, never as a Rust enum.

use flowi::{
    GamepadAxis, GamepadAxisData, GamepadButton, GamepadButtonData, GamepadEvent, GamepadEventKind,
    InvalidGamepadEvent, InvalidMouseEvent, InvalidWindowEvent, MouseButton, MouseButtonData,
    MouseEvent, MouseEventKind, MouseMoveData, MouseRelativeData, MouseWheelData, SafeGamepadEvent,
    SafeMouseEvent, SafeWindowEvent, WindowEvent, WindowEventKind, WindowResizeData,
};

/// Write an integer over the bytes at `offset` of a C struct.
fn poke<T>(event: &mut T, offset: usize, value: u32) {
    // SAFETY: every caller passes the offset of a 4-byte field inside the struct, and the integer is
    // read back only as an integer.
    unsafe {
        (event as *mut T)
            .cast::<u8>()
            .add(offset)
            .cast::<u32>()
            .write_unaligned(value);
    }
}

/// Fill the words of `event` from `from` up to `to` with a value no enum and no bool has.
fn poison<T>(event: &mut T, from: usize, to: usize) {
    for offset in (from..to).step_by(4) {
        poke(event, offset, 0x0707_0707);
    }
}

fn mouse_button_data() -> MouseButtonData {
    MouseButtonData {
        button: MouseButton::Middle,
        x: 3.0,
        y: 4.0,
        click_count: 2,
    }
}

fn every_mouse_event() -> Vec<SafeMouseEvent> {
    [
        // The bit patterns a Debug comparison would hide: negative zero and a NaN.
        MouseEventKind::Move(MouseMoveData {
            x: -0.0,
            y: f32::NAN,
            delta_x: 0.5,
            delta_y: -0.5,
        }),
        MouseEventKind::ButtonDown(mouse_button_data()),
        MouseEventKind::ButtonUp(mouse_button_data()),
        MouseEventKind::Wheel(MouseWheelData { x: 0.0, y: -1.0 }),
        MouseEventKind::Enter,
        MouseEventKind::Leave,
        MouseEventKind::RelativeMotion(MouseRelativeData {
            delta_x: 7.0,
            delta_y: 8.0,
        }),
    ]
    .into_iter()
    .map(|kind| SafeMouseEvent {
        kind,
        timestamp: 42,
    })
    .collect()
}

fn every_gamepad_event() -> Vec<SafeGamepadEvent> {
    let button = GamepadButtonData {
        button: GamepadButton::Dpad_Left,
    };
    [
        GamepadEventKind::ButtonDown(button),
        GamepadEventKind::ButtonUp(button),
        GamepadEventKind::AxisMotion(GamepadAxisData {
            axis: GamepadAxis::Left_Trigger,
            value: -0.25,
        }),
        GamepadEventKind::Connected,
        GamepadEventKind::Disconnected,
    ]
    .into_iter()
    .map(|kind| SafeGamepadEvent {
        kind,
        gamepad_id: 3,
        timestamp: 42,
    })
    .collect()
}

fn every_window_event() -> Vec<SafeWindowEvent<'static>> {
    [
        WindowEventKind::None,
        WindowEventKind::FocusGained,
        WindowEventKind::FocusLost,
        WindowEventKind::Resize(WindowResizeData {
            width: 640,
            height: 480,
        }),
        WindowEventKind::Close,
        WindowEventKind::DropFile {
            path: "/tmp/kick.wav",
            x: 9.0,
            y: 10.0,
        },
    ]
    .into_iter()
    .map(|kind| SafeWindowEvent {
        kind,
        timestamp: 42,
    })
    .collect()
}

/// Bit-for-bit equality, so a changed float or a NaN that lost its bits fails.
fn same_f32(a: f32, b: f32) -> bool {
    a.to_bits() == b.to_bits()
}

fn same_move(a: MouseMoveData, b: MouseMoveData) -> bool {
    same_f32(a.x, b.x)
        && same_f32(a.y, b.y)
        && same_f32(a.delta_x, b.delta_x)
        && same_f32(a.delta_y, b.delta_y)
}

fn same_button(a: MouseButtonData, b: MouseButtonData) -> bool {
    a.button == b.button
        && same_f32(a.x, b.x)
        && same_f32(a.y, b.y)
        && a.click_count == b.click_count
}

fn assert_same_mouse(back: &SafeMouseEvent, event: &SafeMouseEvent) {
    assert_eq!(back.timestamp, event.timestamp, "{event:?}");
    let same = match (back.kind, event.kind) {
        (MouseEventKind::Move(a), MouseEventKind::Move(b)) => same_move(a, b),
        (MouseEventKind::ButtonDown(a), MouseEventKind::ButtonDown(b))
        | (MouseEventKind::ButtonUp(a), MouseEventKind::ButtonUp(b)) => same_button(a, b),
        (MouseEventKind::Wheel(a), MouseEventKind::Wheel(b)) => {
            same_f32(a.x, b.x) && same_f32(a.y, b.y)
        }
        (MouseEventKind::RelativeMotion(a), MouseEventKind::RelativeMotion(b)) => {
            same_f32(a.delta_x, b.delta_x) && same_f32(a.delta_y, b.delta_y)
        }
        (a, b) => core::mem::discriminant(&a) == core::mem::discriminant(&b),
    };
    assert!(same, "{back:?} is not {event:?}");
}

fn assert_same_gamepad(back: &SafeGamepadEvent, event: &SafeGamepadEvent) {
    assert_eq!(back.timestamp, event.timestamp, "{event:?}");
    assert_eq!(back.gamepad_id, event.gamepad_id, "{event:?}");
    let same = match (back.kind, event.kind) {
        (GamepadEventKind::ButtonDown(a), GamepadEventKind::ButtonDown(b))
        | (GamepadEventKind::ButtonUp(a), GamepadEventKind::ButtonUp(b)) => a.button == b.button,
        (GamepadEventKind::AxisMotion(a), GamepadEventKind::AxisMotion(b)) => {
            a.axis == b.axis && same_f32(a.value, b.value)
        }
        (GamepadEventKind::Connected, GamepadEventKind::Connected)
        | (GamepadEventKind::Disconnected, GamepadEventKind::Disconnected) => true,
        _ => false,
    };
    assert!(same, "{back:?} is not {event:?}");
}

fn assert_same_window(back: &SafeWindowEvent, event: &SafeWindowEvent) {
    assert_eq!(back.timestamp, event.timestamp, "{event:?}");
    let same = match (back.kind, event.kind) {
        (WindowEventKind::None, WindowEventKind::None)
        | (WindowEventKind::FocusGained, WindowEventKind::FocusGained)
        | (WindowEventKind::FocusLost, WindowEventKind::FocusLost)
        | (WindowEventKind::Close, WindowEventKind::Close) => true,
        (WindowEventKind::Resize(a), WindowEventKind::Resize(b)) => {
            a.width == b.width && a.height == b.height
        }
        (
            WindowEventKind::DropFile { path: p, x, y },
            WindowEventKind::DropFile {
                path: q,
                x: x2,
                y: y2,
            },
        ) => p == q && same_f32(x, x2) && same_f32(y, y2),
        _ => false,
    };
    assert!(same, "{back:?} is not {event:?}");
}

#[test]
fn every_mouse_event_survives_the_trip_through_the_c_struct() {
    for event in every_mouse_event() {
        let c = MouseEvent::from(event);
        // SAFETY: c is a whole, aligned MouseEvent built by From.
        let back =
            unsafe { SafeMouseEvent::try_from_raw(&c) }.expect("a valid event converts back");
        assert_same_mouse(&back, &event);
    }
}

#[test]
fn every_gamepad_event_survives_the_trip_through_the_c_struct() {
    for event in every_gamepad_event() {
        let c = GamepadEvent::from(event);
        // SAFETY: c is a whole, aligned GamepadEvent built by From.
        let back =
            unsafe { SafeGamepadEvent::try_from_raw(&c) }.expect("a valid event converts back");
        assert_same_gamepad(&back, &event);
    }
}

#[test]
fn every_window_event_survives_the_trip_through_the_c_struct() {
    for event in every_window_event() {
        let c = WindowEvent::from(event);
        // SAFETY: c is a whole, aligned WindowEvent built by From, and its path is 'static.
        let back =
            unsafe { SafeWindowEvent::try_from_raw(&c) }.expect("a valid event converts back");
        assert_same_window(&back, &event);
    }
}

#[test]
fn a_mouse_event_with_a_stray_tag_or_enum_value_is_refused() {
    let tag_at = core::mem::offset_of!(MouseEvent, r#type);
    let button_at =
        core::mem::offset_of!(MouseEvent, payload) + core::mem::offset_of!(MouseButtonData, button);

    for event in every_mouse_event() {
        let mut c = MouseEvent::from(event);
        poke(&mut c, tag_at, 99);
        // SAFETY: c is a whole MouseEvent; the conversion checks the tag before it reads it.
        let err = unsafe { SafeMouseEvent::try_from_raw(&c) }.expect_err("tag 99 is refused");
        assert_eq!(
            err,
            InvalidMouseEvent {
                field: "type",
                value: 99
            }
        );
    }

    // MouseButton has five buttons; 5 is its Count, and 7 is past it.
    for stray in [5, 7, u32::MAX] {
        for event in every_mouse_event() {
            if !matches!(
                event.kind,
                MouseEventKind::ButtonDown(_) | MouseEventKind::ButtonUp(_)
            ) {
                continue;
            }
            let mut c = MouseEvent::from(event);
            poke(&mut c, button_at, stray);
            // SAFETY: c is a whole MouseEvent; the conversion checks the button before it reads it.
            let err =
                unsafe { SafeMouseEvent::try_from_raw(&c) }.expect_err("the button is refused");
            assert_eq!(
                err,
                InvalidMouseEvent {
                    field: "button.button",
                    value: stray
                }
            );
        }
    }
}

#[test]
fn a_gamepad_event_with_a_stray_tag_or_enum_value_is_refused() {
    let tag_at = core::mem::offset_of!(GamepadEvent, r#type);
    let button_at = core::mem::offset_of!(GamepadEvent, payload)
        + core::mem::offset_of!(GamepadButtonData, button);
    let axis_at =
        core::mem::offset_of!(GamepadEvent, payload) + core::mem::offset_of!(GamepadAxisData, axis);

    for event in every_gamepad_event() {
        let mut c = GamepadEvent::from(event);
        poke(&mut c, tag_at, 99);
        // SAFETY: c is a whole GamepadEvent; the conversion checks the tag before it reads it.
        let err = unsafe { SafeGamepadEvent::try_from_raw(&c) }.expect_err("tag 99 is refused");
        assert_eq!(
            err,
            InvalidGamepadEvent {
                field: "type",
                value: 99
            }
        );
    }

    // GamepadButton has 15 buttons and GamepadAxis 6; each Count is no value.
    for (stray_button, stray_axis) in [(15, 6), (16, 7), (u32::MAX, u32::MAX)] {
        for event in every_gamepad_event() {
            let (offset, field, stray) = match event.kind {
                GamepadEventKind::ButtonDown(_) | GamepadEventKind::ButtonUp(_) => {
                    (button_at, "button.button", stray_button)
                }
                GamepadEventKind::AxisMotion(_) => (axis_at, "axis.axis", stray_axis),
                _ => continue,
            };
            let mut c = GamepadEvent::from(event);
            poke(&mut c, offset, stray);
            // SAFETY: c is a whole GamepadEvent; the conversion checks the value before it reads it.
            let err =
                unsafe { SafeGamepadEvent::try_from_raw(&c) }.expect_err("the value is refused");
            assert_eq!(
                err,
                InvalidGamepadEvent {
                    field,
                    value: stray
                }
            );
        }
    }
}

#[test]
fn a_window_event_with_a_stray_tag_is_refused() {
    let tag_at = core::mem::offset_of!(WindowEvent, r#type);
    for event in every_window_event() {
        let mut c = WindowEvent::from(event);
        poke(&mut c, tag_at, 99);
        // SAFETY: c is a whole WindowEvent; the conversion checks the tag before it reads it.
        let err = unsafe { SafeWindowEvent::try_from_raw(&c) }.expect_err("tag 99 is refused");
        assert_eq!(
            err,
            InvalidWindowEvent {
                field: "type",
                value: 99
            }
        );
    }
}

#[test]
fn a_sentinel_built_in_safe_code_converts_to_c_but_is_refused_when_read_back() {
    let event = SafeMouseEvent {
        kind: MouseEventKind::ButtonDown(MouseButtonData {
            button: MouseButton::Count,
            x: 0.0,
            y: 0.0,
            click_count: 1,
        }),
        timestamp: 0,
    };
    let c = MouseEvent::from(event);
    // SAFETY: c is a whole MouseEvent built by From.
    let err = unsafe { SafeMouseEvent::try_from_raw(&c) }.expect_err("Count is no button");
    assert_eq!(err.field, "button.button");
    assert_eq!(err.value, MouseButton::Count as u32);
}

/// Poison the payload of each event's C struct and read it back: the key of what comes out must be the
/// key of what went in, and the read must not have looked at the poison.
fn assert_unselected_arm_is_not_read<
    E: Copy + core::fmt::Debug,
    C,
    K: PartialEq + core::fmt::Debug,
>(
    events: impl Iterator<Item = E>,
    to_c: impl Fn(E) -> C,
    payload: (usize, usize),
    read: impl Fn(&C) -> Result<K, String>,
    key: impl Fn(E) -> K,
) {
    for event in events {
        let mut c = to_c(event);
        poison(&mut c, payload.0, payload.1);
        let back =
            read(&c).unwrap_or_else(|err| panic!("{event:?}: an unselected arm was read: {err}"));
        assert_eq!(back, key(event), "{event:?}");
    }
}

/// The test of one event type: poison the payload of each of `$events`, which are the ones whose tag selects
/// no arm that holds an enum, and expect the read to succeed with the same variant.
macro_rules! assert_unselected_arm_is_not_read_for {
    ($c:ty, $safe:ident, $events:expr) => {
        assert_unselected_arm_is_not_read(
            $events,
            <$c>::from,
            (
                core::mem::offset_of!($c, payload),
                core::mem::offset_of!($c, timestamp),
            ),
            |c| {
                // SAFETY: the struct is a whole C event whose payload is plain numbers under this tag.
                unsafe { $safe::try_from_raw(c) }
                    .map(|back| core::mem::discriminant(&back.kind))
                    .map_err(|err| err.to_string())
            },
            |event| core::mem::discriminant(&event.kind),
        )
    };
}

// Every word of the payload is a value no enum has and no bool byte is. Under a tag that selects an arm
// of plain numbers, or no arm, nothing in it is read as an enum.

#[test]
fn invalid_data_in_an_arm_the_tag_does_not_select_is_not_looked_at() {
    assert_unselected_arm_is_not_read_for!(
        MouseEvent,
        SafeMouseEvent,
        every_mouse_event().into_iter().filter(|event| !matches!(
            event.kind,
            MouseEventKind::ButtonDown(_) | MouseEventKind::ButtonUp(_)
        ))
    );
    assert_unselected_arm_is_not_read_for!(
        GamepadEvent,
        SafeGamepadEvent,
        every_gamepad_event().into_iter().filter(|event| matches!(
            event.kind,
            GamepadEventKind::Connected | GamepadEventKind::Disconnected
        ))
    );
    assert_unselected_arm_is_not_read_for!(
        WindowEvent,
        SafeWindowEvent,
        every_window_event()
            .into_iter()
            .filter(|event| !matches!(event.kind, WindowEventKind::DropFile { .. }))
    );
}

#[test]
fn the_same_poisoned_payload_is_refused_under_a_tag_that_selects_the_button_arm() {
    // The payload words that read fine under a plain-number tag are refused once a button tag selects them.
    let mouse_at = core::mem::offset_of!(MouseEvent, payload);
    let mouse_end = core::mem::offset_of!(MouseEvent, timestamp);
    let mut c = MouseEvent::from(SafeMouseEvent {
        kind: MouseEventKind::ButtonDown(mouse_button_data()),
        timestamp: 0,
    });
    poison(&mut c, mouse_at, mouse_end);
    // SAFETY: c is a whole MouseEvent; the conversion checks the button before it reads it.
    let err =
        unsafe { SafeMouseEvent::try_from_raw(&c) }.expect_err("the poisoned button is refused");
    assert_eq!(err.field, "button.button");
}

#[test]
fn the_error_names_the_field_and_the_value() {
    let err = InvalidMouseEvent {
        field: "type",
        value: 99,
    };
    assert_eq!(err.to_string(), "invalid MouseEvent: field `type` holds 99");
}
