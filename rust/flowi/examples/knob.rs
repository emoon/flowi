//! A reference knob, assembled from public primitives only - the generic [`Item::behavior`] Signal to detect
//! the grab, a hidden relative drag ([`UiInput::mouse_hidden_drag_begin`] / [`UiInput::mouse_drag_delta`] /
//! [`UiInput::mouse_hidden_drag_end`]) for the unbounded scrub, and [`Painter::circle`] / [`Painter::arc`] for
//! the body and indicator. It needs a display; run it and drag the knob up/down:
//!
//! ```text
//! cargo run -p flowi --example knob
//! ```
//!
//! Interaction: drag up to raise the value, down to lower it; hold Shift for a fine drag; double-click to
//! reset to the default.

use flowi::{
    col, element_state, fixed, grow, layout, Application, ArcFlags, Color, Item, ItemFlags,
    KeyModifier, Painter, Text, UiInput,
};
use std::f32::consts::PI;

const KNOB_ID: &str = "knob";

// Angles are measured from 12 o'clock, positive sweeping clockwise: the indicator spans 270°, from the min value
// at 7:30 (−135°) to a full value at 4:30 (+135°).
const ARC_START: f32 = -0.75 * PI;
const ARC_SWEEP: f32 = 1.5 * PI;

// Hidden-drag sensitivity: 1.0 reads the raw per-frame pixel delta as-is.
const DRAG_SCALE: f32 = 1.0;
// Value units per scaled pixel - the normalized 0..1 range covers a 200px drag.
const SENSITIVITY: f32 = 1.0 / 200.0;
const FINE_FACTOR: f32 = 0.2;
const DEFAULT_VALUE: f32 = 0.5;

/// The normalized value the control writes, plus the previous frame's dragging flag.
struct Knob {
    value: f32,
    dragging: bool,
}

// Painter content draws under an element's own background, so the element has no fill.
fn knob(state: &mut Knob) {
    layout! { KNOB_ID, width: fixed!(72.0), height: fixed!(72.0) {
        let sig = Item::behavior(
            KNOB_ID,
            ItemFlags::CLICKABLE | ItemFlags::DRAGGABLE | ItemFlags::DOUBLE_CLICK,
        );

        if sig.dragging && !state.dragging {
            UiInput::mouse_hidden_drag_begin(DRAG_SCALE);
        } else if !sig.dragging && state.dragging {
            UiInput::mouse_hidden_drag_end();
        }
        state.dragging = sig.dragging;

        // Dragging up is negative screen-y, so subtract the delta to raise the value.
        if sig.dragging {
            let mut dy = UiInput::mouse_drag_delta().y;
            if UiInput::keyboard_modifiers().contains(KeyModifier::SHIFT) {
                dy *= FINE_FACTOR;
            }
            state.value = (state.value - dy * SENSITIVITY).clamp(0.0, 1.0);
        }

        if sig.double_clicked {
            state.value = DEFAULT_VALUE;
        }

        // The resolved rect is the previous frame's, in logical px.
        let rect = element_state(KNOB_ID);
        if rect.found {
            let cx = rect.pos.x + rect.size.x / 2.0;
            let cy = rect.pos.y + rect.size.y / 2.0;
            let r = rect.size.x.min(rect.size.y) / 2.0 - 6.0;
            let body = Color { r: 50, g: 50, b: 62, a: 255 };
            let ring = Color { r: 90, g: 90, b: 110, a: 255 };
            let indicator = Color { r: 90, g: 170, b: 255, a: 255 };
            Painter::circle(cx, cy, r, body, ring, 1.5);
            Painter::arc(cx, cy, r - 2.0, 4.0, ARC_START, state.value * ARC_SWEEP, indicator, ArcFlags::empty());
        }
    }}
}

fn main() {
    let app = Application::new(800, 600, false);
    let state = Knob {
        value: DEFAULT_VALUE,
        dragging: false,
    };
    app.run(state, |s: &mut Knob| {
        col! { width: grow!(), height: grow!(), color: [20, 20, 28] {
            knob(s);
            Text::emit(format!("{:.2}", s.value));
        }}
    });
}
