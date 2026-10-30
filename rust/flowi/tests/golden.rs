//! Headless clear-color check.

use flowi::Application;

// Default clear color (theme surface.base).
const CLEAR: [u8; 3] = [14, 14, 16];

#[test]
fn headless_clear_color() {
    let mut app = Application::new(64, 64, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {}),
        "first tick should keep running"
    );

    let pv = app.read_pixels().expect("render backend readback");
    assert_eq!(pv.width, 64, "readback width");
    assert_eq!(pv.height, 64, "readback height");
    assert_eq!(pv.channels, 3, "software backend reads back RGB24");
    assert_eq!(pv.pixels.len(), 64 * 64 * 3, "tightly-packed RGB24");

    for (i, px) in pv.pixels.as_chunks::<3>().0.iter().enumerate() {
        assert_eq!(
            px,
            &CLEAR,
            "pixel {} ({},{}) is {:?}, expected clear color {:?}",
            i,
            i % 64,
            i / 64,
            px,
            CLEAR
        );
    }
}
