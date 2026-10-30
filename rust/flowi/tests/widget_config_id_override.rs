use flowi::{col, element_state, grow, row, Application, Button, WidgetConfig};

mod common;

// The label's auto-derived id must be displaced by the override.
const OVERRIDE_ID: &str = "wc_custom_button";
const LABEL: &str = "Click me";

// The button sits at the layout origin, so (10,10) is inside it at ui_scale 1.0.
const HIT_X: i32 = 10;
const HIT_Y: i32 = 10;

#[derive(Default)]
struct Probe {
    override_found: bool,
    label_found: bool,
    hovered: bool,
}

fn frame(p: &mut Probe) {
    col! { width: grow!(), height: grow!() {
        row! { {
            let sig = Button::new(
                LABEL,
                WidgetConfig {
                    id_override: OVERRIDE_ID,
                    ..Default::default()
                },
            );
            p.hovered = sig.hovered;
        } }
    }}
    p.override_found = element_state(OVERRIDE_ID).found;
    p.label_found = element_state(LABEL).found;
}

#[test]
fn headless_widget_config_id_override() {
    let mut app = Application::new(200, 120, true);
    let mut probe = Probe::default();

    // The pulled state reflects the previous frame's resolved layout, so settle a few frames
    // with the cursor parked over the button.
    app.inject_mouse_move(HIT_X, HIT_Y);
    common::tick_n(&mut app, &mut probe, frame, 4, "settle");

    assert!(
        probe.override_found,
        "the button should lay out under its `id_override` (\"{OVERRIDE_ID}\"), so the pulled element state finds it"
    );
    assert!(
        !probe.label_found,
        "the label-derived id (\"{LABEL}\") must not be registered once `id_override` is set"
    );
    assert!(
        probe.hovered,
        "a button with a non-default config should still hover when the cursor is over it"
    );
}
