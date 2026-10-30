//! An injected OS file drop reaches the frame body through the dropped-file queries, carrying its
//! path and the release point, and is gone again on the next frame.

use flowi::{Application, UiInput};

mod common;

#[derive(Default)]
struct DropProbe {
    /// One entry per frame since the probe was cleared: that frame's (path, x, y) list.
    frames: Vec<Vec<(String, f32, f32)>>,
}

fn frame(p: &mut DropProbe) {
    let mut dropped = Vec::new();
    for index in 0..UiInput::dropped_file_count() {
        let position = UiInput::dropped_file_position(index);
        dropped.push((UiInput::dropped_file(index), position.x, position.y));
    }
    p.frames.push(dropped);
}

#[test]
fn headless_dropped_files() {
    let mut app = Application::new(200, 120, true);
    let mut probe = DropProbe::default();

    common::tick_n(&mut app, &mut probe, frame, 2, "settle");
    for dropped in &probe.frames {
        assert!(
            dropped.is_empty(),
            "no drop should report before one is injected"
        );
    }

    probe.frames.clear();
    app.inject_drop_file("/tmp/kick.wav", 48.0, 12.0);
    app.inject_drop_file("/tmp/snare.wav", 48.0, 12.0);
    common::tick_n(&mut app, &mut probe, frame, 2, "drop");

    assert_eq!(
        probe.frames[0],
        vec![
            ("/tmp/kick.wav".to_owned(), 48.0, 12.0),
            ("/tmp/snare.wav".to_owned(), 48.0, 12.0),
        ],
        "both dropped paths reach the frame the pump drained them on, in delivery order, with the release point"
    );
    assert!(
        probe.frames[1].is_empty(),
        "a drop belongs to one frame: the next frame reads none"
    );
}
