//! The placeholder is drawn inside the field's existing box while the buffer is empty, in the theme's
//! secondary text colour, so it is observed by counting light pixels in the rendered frame.

use flowi::{col, element_state, grow, Application, TextInput, TextInputConfig, WidgetConfig};

mod common;

const OVERRIDE_ID: &str = "ti_search_box";
const LABEL: &str = "Search";
const PLACEHOLDER: &str = "Type to filter presets";

/// Channel floor for glyph ink: the secondary text colour is {153, 153, 153} over far darker surfaces, so a
/// pixel above this on all three channels is text coverage rather than chrome.
const INK_FLOOR: u8 = 60;

struct Probe {
    input: TextInput,
    /// "" renders no placeholder; a set string renders one while the buffer is empty.
    placeholder: &'static str,
    found: bool,
}

fn frame(p: &mut Probe) {
    col! { width: grow!(), height: grow!() {
        p.input.show(
            LABEL,
            TextInputConfig {
                placeholder: p.placeholder,
                widget: WidgetConfig {
                    id_override: OVERRIDE_ID,
                    ..Default::default()
                },
                ..Default::default()
            },
        );
    }}

    p.found = element_state(OVERRIDE_ID).found;
}

/// Text fades in over a few frames; this settles well past the transition.
fn settle_and_count_ink(app: &mut Application, probe: &mut Probe) -> usize {
    common::tick_n(app, probe, frame, 12, "settle");

    let pv = app.read_pixels().expect("render backend readback");
    assert_eq!(pv.channels, 3, "software backend reads back RGB24");
    pv.pixels
        .as_chunks::<3>()
        .0
        .iter()
        .filter(|px| px.iter().all(|c| *c > INK_FLOOR))
        .count()
}

#[test]
fn headless_text_input_placeholder() {
    let mut app = Application::new(320, 120, true);
    let mut probe = Probe {
        input: TextInput::new(),
        placeholder: "",
        found: false,
    };

    // No placeholder: the field renders only its chrome.
    let bare_ink = settle_and_count_ink(&mut app, &mut probe);
    assert!(
        probe.found,
        "the field should lay out under its `id_override` (\"{OVERRIDE_ID}\"), which requires the embedded \
         widget config to have reached C"
    );

    // Same field, same empty buffer - only the placeholder was added.
    probe.placeholder = PLACEHOLDER;
    let placeholder_ink = settle_and_count_ink(&mut app, &mut probe);

    // A 22-character run at the default font size inks over an order of magnitude more than the bare field.
    assert!(
        placeholder_ink > bare_ink * 5,
        "a field carrying the placeholder \"{PLACEHOLDER}\" should ink far more text pixels than the same \
         field without one ({placeholder_ink} vs {bare_ink}); a comparable count means the placeholder bytes \
         never reached C"
    );
}
