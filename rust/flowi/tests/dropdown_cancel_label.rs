//! The dropdown's cancel row is emitted under `"<button id>_cancel"`.

use flowi::{col, element_state, grow, Application, Dropdown, DropdownConfig, WidgetConfig};

mod common;

const OVERRIDE_ID: &str = "dd_preset_picker";
const LABEL: &str = "Preset";
// Much longer than the default "Cancel".
const LONG_CANCEL: &str = "Dismiss the preset picker";

struct Probe {
    cancel_label: &'static str,
    /// Width of the cancel row's element, in logical px.
    cancel_width: f32,
    cancel_found: bool,
}

fn frame(p: &mut Probe) {
    // The popup only lays out while the dropdown is open; holding is_open true emits the cancel row every frame.
    let mut is_open = true;
    let mut selected = -1i32;

    col! { width: grow!(), height: grow!() {
        Dropdown::new(
            LABEL,
            &mut is_open,
            &["Warm", "Bright"],
            &mut selected,
            DropdownConfig {
                cancel_label: p.cancel_label,
                widget: WidgetConfig {
                    id_override: OVERRIDE_ID,
                    ..Default::default()
                },
                show_cancel: true,
                ..Default::default()
            },
        );
    }}

    let cancel = element_state(format!("{OVERRIDE_ID}_cancel"));
    p.cancel_found = cancel.found;
    p.cancel_width = cancel.size.x;
}

/// Ticks until the pulled state reflects resolved layout (it lags one frame).
fn settle(app: &mut Application, probe: &mut Probe) -> f32 {
    common::tick_n(app, probe, frame, 4, "settle");
    assert!(
        probe.cancel_found,
        "the open dropdown should lay out a cancel row under \"{OVERRIDE_ID}_cancel\", which requires the \
         embedded widget config's id_override to have reached C"
    );
    probe.cancel_width
}

#[test]
fn headless_dropdown_cancel_label() {
    let mut app = Application::new(320, 240, true);
    let mut probe = Probe {
        cancel_label: "",
        cancel_width: 0.0,
        cancel_found: false,
    };

    // Empty label: the default "Cancel" is used.
    let default_width = settle(&mut app, &mut probe);

    probe.cancel_label = LONG_CANCEL;
    let long_width = settle(&mut app, &mut probe);

    assert!(
        long_width > default_width,
        "a cancel row labelled \"{LONG_CANCEL}\" should lay out wider than the C default \"Cancel\" \
         ({long_width} vs {default_width} px); an equal width means the `cancel_label` bytes never reached C"
    );
}

#[test]
fn default_config_shows_cancel_at_declared_min_width() {
    let config = DropdownConfig::default();
    assert!(config.show_cancel);
    assert_eq!(config.min_width, 200.0);
}
