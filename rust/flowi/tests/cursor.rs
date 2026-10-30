use flowi::{Application, Cursor, CursorShape};

struct CursorProbe {
    request: bool,
}

fn frame(p: &mut CursorProbe) {
    if p.request {
        // Last-writer-wins within a frame: the second request (Hand) is the one that should reach the backend.
        Cursor::set(Cursor::system(CursorShape::Crosshair));
        Cursor::set(Cursor::system(CursorShape::Hand));
        Cursor::set_visible(false);
    }
}

#[test]
fn headless_cursor_request() {
    let mut app = Application::new(120, 80, true);
    let mut probe = CursorProbe { request: true };

    assert!(
        app.tick(&mut probe, frame),
        "request tick should keep running"
    );
    assert_eq!(
        app.headless_cursor_shape(),
        CursorShape::Hand,
        "the last set_cursor of the frame (Hand) reaches the input backend"
    );

    probe.request = false;
    assert!(
        app.tick(&mut probe, frame),
        "no-request tick should keep running"
    );
    assert_eq!(
        app.headless_cursor_shape(),
        CursorShape::Arrow,
        "a frame with no set_cursor resets the cursor to the arrow"
    );
}
