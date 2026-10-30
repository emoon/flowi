//! The scroll and floating macro expansions type-check and the engine accepts the configs; pixels are not
//! asserted.

use flowi::{
    col, fixed, floating, grow, scroll, Application, FloatingAttachPoint, FloatingPointerCapture,
    ScrollbarDisplayMode,
};

#[test]
fn macros_scroll_and_floating_run() {
    let mut app = Application::new(64, 64, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            col! { "root", width: grow!(), height: grow!() {
                // A taller-than-viewport child makes overflow meaningful.
                scroll! { "scroller", width: fixed!(40.0), height: fixed!(30.0),
                          scrollbar: true, scrollbar_mode: ScrollbarDisplayMode::Always {
                    col! { width: grow!(), height: fixed!(80.0), color: [10, 20, 30] {} }
                }}
                // The three attach/capture fields carry non-default values (defaults are Capture / LeftTop /
                // LeftTop).
                floating! { "popup", width: fixed!(20.0), height: fixed!(20.0), offset: [5.0, 5.0], z_index: 1,
                            pointer_capture: FloatingPointerCapture::Passthrough,
                            element_attach: FloatingAttachPoint::RightBottom,
                            parent_attach: FloatingAttachPoint::CenterCenter {
                    col! { width: grow!(), height: grow!(), color: [200, 200, 200] {} }
                }}
            }}
        }),
        "first tick should keep running"
    );

    let pv = app.read_pixels().expect("render backend readback");
    assert_eq!(pv.pixels.len(), 64 * 64 * 3, "tightly-packed RGB24");
}
