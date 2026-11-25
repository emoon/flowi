//! An embedding host pumps its SDL window's events into the application with one call.

use std::{cell::RefCell, rc::Rc, sync::Mutex};

use flowi::{Application, EmbeddedSettings, PadType, SdlEventPump, SdlPumpConfig};

/// One Application at a time, and one SDL event queue.
static ONE_AT_A_TIME: Mutex<()> = Mutex::new(());

const SDL_QUIT: u32 = 0x100;
const SDL_EVENT_SIZE: usize = 56;

#[link(name = "SDL2")]
extern "C" {
    fn SDL_PushEvent(event: *mut u8) -> i32;
    fn SDL_JoystickAttachVirtual(kind: i32, axes: i32, buttons: i32, hats: i32) -> i32;
    fn SDL_JoystickDetachVirtual(device: i32) -> i32;
}

fn push_quit() {
    let mut event = [0u8; SDL_EVENT_SIZE];
    event[..4].copy_from_slice(&SDL_QUIT.to_ne_bytes());
    // SAFETY: a zeroed buffer as large as SDL_Event with only the type set, which SDL copies.
    assert_eq!(unsafe { SDL_PushEvent(event.as_mut_ptr()) }, 1);
}

#[test]
fn the_pump_runs_until_the_user_asks_to_quit() {
    let _serial = ONE_AT_A_TIME.lock().unwrap_or_else(|e| e.into_inner());
    let mut app = Application::embedded(EmbeddedSettings::new(64, 48)).expect("embedded flowi");
    let config = SdlPumpConfig {
        window_id: 0,
        controller_db: "",
        on_pad: Some(Box::new(|_, _| {})),
    };
    let mut pump = SdlEventPump::new(&app, config).expect("sdl pump");

    assert!(app.pump_sdl_events(&mut pump, 1.0));
    push_quit();
    assert!(!app.pump_sdl_events(&mut pump, 1.0));
    assert!(app.pump_sdl_events(&mut pump, 1.0));
}

#[test]
fn raw_pads_reach_the_callback_with_their_identity() {
    let _serial = ONE_AT_A_TIME.lock().unwrap_or_else(|e| e.into_inner());
    const SDL_JOYSTICK_TYPE_GAMECONTROLLER: i32 = 1;
    let mut app = Application::embedded(EmbeddedSettings::new(64, 48)).expect("embedded flowi");
    let seen = Rc::new(RefCell::new(Vec::new()));
    let log = Rc::clone(&seen);
    let config = SdlPumpConfig {
        window_id: 0,
        controller_db: "",
        on_pad: Some(Box::new(move |_, pad| {
            log.borrow_mut()
                .push((pad.name.to_owned(), pad.pad_type, pad.has_guide));
        })),
    };
    let mut pump = SdlEventPump::new(&app, config).expect("sdl pump");
    // SAFETY: a plain SDL call; the pump above started the subsystems the virtual pad needs.
    let device = unsafe { SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0) };
    assert!(device >= 0, "virtual joystick unavailable");

    assert!(app.pump_sdl_events(&mut pump, 1.0));
    // SAFETY: a plain SDL call on the device attached above.
    unsafe { SDL_JoystickDetachVirtual(device) };
    assert!(app.pump_sdl_events(&mut pump, 1.0));

    let seen = seen.borrow();
    assert!(
        seen.len() >= 2,
        "a connect and a disconnect, got {}",
        seen.len()
    );
    assert!(seen.iter().all(|(name, _, _)| !name.is_empty()));
    assert!(seen.iter().all(|(_, kind, _)| *kind == PadType::Unknown));
}
