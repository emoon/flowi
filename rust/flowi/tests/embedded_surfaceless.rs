//! A zero render size opens no surface and no UI context, so there is no frame to prime.

#![forbid(unsafe_code)]

use flowi::{Application, EmbeddedSettings};

#[test]
fn a_surfaceless_embedded_application_comes_up_and_reads_back_nothing() {
    let mut app = Application::embedded(EmbeddedSettings::new(0, 0))
        .expect("a surfaceless embedded application should come up");

    assert!(
        app.pixels().is_none(),
        "an application with no render surface has nothing to read back"
    );
    assert!(
        app.read_pixels().is_none(),
        "an application with no render surface has no pixel copy to take"
    );
}
