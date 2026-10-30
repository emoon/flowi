use std::cell::Cell;
use std::rc::Rc;

use flowi::{col, grow, Application, WindowSettings};

// The app owns each window's state, so frames are counted through a shared cell. Element
// ids are app-global across every window of a tick, so each window names its root.
fn count_frame(root: &str, counter: &mut Rc<Cell<u32>>) {
    col! { root, width: grow!(), height: grow!() {} }
    counter.set(counter.get() + 1);
}

fn main_frame(counter: &mut Rc<Cell<u32>>) {
    count_frame("main_root", counter);
}

fn second_frame(counter: &mut Rc<Cell<u32>>) {
    count_frame("second_root", counter);
}

#[test]
fn headless_multi_window_open_close() {
    let mut app = Application::new(200, 150, true);

    let main_frames = Rc::new(Cell::new(0u32));
    let sec_frames = Rc::new(Cell::new(0u32));

    // The secondary's frame context is owned (boxed) by the app and runs every
    // tick alongside main's.
    let second = app.open_window(
        WindowSettings::new("second", 160, 120),
        sec_frames.clone(),
        second_frame,
    );

    // One tick runs BOTH windows' callbacks, each against its own state: the main
    // window's is the one passed to tick, the second's is the one it was opened
    // with.
    let mut main_state = main_frames.clone();
    assert!(
        app.tick(&mut main_state, main_frame),
        "tick with two windows keeps running"
    );
    assert_eq!(main_frames.get(), 1, "main window's frame ran this tick");
    assert_eq!(
        sec_frames.get(),
        1,
        "second window's frame ran this tick (its own callback + state)"
    );

    // The secondary is torn down at the next tick boundary.
    app.close_window(second);
    assert!(
        app.tick(&mut main_state, main_frame),
        "closing a non-main window leaves the app running"
    );
    assert_eq!(
        main_frames.get(),
        2,
        "main window keeps ticking after the secondary closed"
    );
    assert_eq!(
        sec_frames.get(),
        1,
        "closed secondary window's frame no longer runs (close hit only it)"
    );
}
