//! TextInput::show hands the caller three one-frame edges on the returned Signal: text_changed, submitted and
//! cancelled. A held key reports once, on the frame it is pumped, and idle frames report nothing.

use flowi::{col, grow, set_focus, Application, Key, Signal, TextInput, TextInputConfig};

const LABEL: &str = "edges";

struct Probe {
    input: TextInput,
    signal: Option<Signal>,
}

fn frame(p: &mut Probe) {
    set_focus(LABEL);
    col! { width: grow!(), height: grow!() {
        p.signal = Some(p.input.show(LABEL, TextInputConfig::default()));
    }}
}

fn tick(app: &mut Application, probe: &mut Probe) -> Signal {
    probe.signal = None;
    assert!(app.tick(probe, frame), "tick should keep running");
    probe
        .signal
        .expect("the frame callback should have shown the field")
}

#[test]
fn headless_text_input_signals() {
    let mut app = Application::new(320, 120, true);
    let mut probe = Probe {
        input: TextInput::new(),
        signal: None,
    };
    probe.input.set_text("abc");

    let mut s = tick(&mut app, &mut probe);
    for _ in 0..3 {
        s = tick(&mut app, &mut probe);
    }
    assert!(s.focused, "the field should hold focus once settled");
    assert!(!s.text_changed && !s.submitted && !s.cancelled);

    app.inject_key(Key::Backspace, true);
    let s = tick(&mut app, &mut probe);
    assert!(s.text_changed, "backspace edits the text on its frame");
    assert!(!s.submitted && !s.cancelled);
    assert_eq!(probe.input.text(), "ab");
    app.inject_key(Key::Backspace, false);
    let s = tick(&mut app, &mut probe);
    assert!(!s.text_changed, "the edit edge lasts one frame");

    app.inject_key(Key::Enter, true);
    let s = tick(&mut app, &mut probe);
    assert!(s.submitted, "Enter submits on its frame");
    assert!(!s.cancelled && !s.text_changed);
    let s = tick(&mut app, &mut probe);
    assert!(!s.submitted, "a held key does not repeat the edge");
    app.inject_key(Key::Enter, false);
    let s = tick(&mut app, &mut probe);
    assert!(!s.submitted, "the submit edge lasts one frame");

    app.inject_key(Key::Escape, true);
    let s = tick(&mut app, &mut probe);
    assert!(s.cancelled, "Escape cancels on its frame");
    assert!(!s.submitted && !s.text_changed);
    let s = tick(&mut app, &mut probe);
    assert!(!s.cancelled, "a held key does not repeat the edge");
    app.inject_key(Key::Escape, false);
    let s = tick(&mut app, &mut probe);
    assert!(!s.cancelled, "the cancel edge lasts one frame");
}
