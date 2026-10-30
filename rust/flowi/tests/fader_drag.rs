use flowi::{
    col, element_state, grow, slider_float_with, Application, MouseButton, SliderConfig,
    WidgetConfig,
};

mod common;

// The label's auto-derived id is displaced by TRACK_ID below, so nothing should lay out under it.
const FADER: &str = "Volume";

// The id the slider actually lays out under, via SliderConfig.widget.id_override.
const TRACK_ID: &str = "volume_fader_track";

#[derive(Default)]
struct Fader {
    /// Written in place by the slider each frame; starts at 0.0 (range min).
    value: f32,
    /// Previous-frame track rect, captured inside the frame.
    track_x: f32,
    track_y: f32,
    track_w: f32,
    track_h: f32,
    found: bool,
    label_found: bool,
}

fn frame(s: &mut Fader) {
    col! { width: grow!(), height: grow!() {
        slider_float_with(
            FADER,
            &mut s.value,
            0.0,
            1.0,
            SliderConfig {
                logarithmic: true,
                vertical: true,
                widget: WidgetConfig { id_override: TRACK_ID, ..Default::default() },
                ..Default::default()
            },
        );
        let st = element_state(TRACK_ID);
        s.found = st.found;
        s.track_x = st.pos.x;
        s.track_y = st.pos.y;
        s.track_w = st.size.x;
        s.track_h = st.size.y;
        s.label_found = element_state(FADER).found;
    }}
}

#[test]
fn headless_fader_drag_moves_value() {
    let mut app = Application::new(400, 200, true);
    let mut state = Fader::default();

    // Settle so the track rect and the previous-frame width the slider's drag math reads are established.
    common::tick_n(&mut app, &mut state, frame, 4, "settle");
    assert!(
        state.found,
        "the fader track should lay out under its `widget.id_override` (\"{TRACK_ID}\") after settle, which \
         requires `SliderConfig.widget` to have reached C"
    );
    assert!(
        !state.label_found,
        "the label-derived id (\"{FADER}\") must not be registered once `widget.id_override` is set"
    );
    let initial = state.value;

    // ui_scale is 1.0, so the logical rect maps 1:1 to injected window pixels.
    let cy = (state.track_y + state.track_h / 2.0) as i32;
    let left_x = (state.track_x + 4.0) as i32;
    let right_x = (state.track_x + state.track_w - 4.0) as i32;

    // Dragging right raises the value. Injected input applies a tick later and drag state trails by a frame,
    // so tick generously between and after the moves.
    app.inject_mouse_move(left_x, cy);
    assert!(
        app.tick(&mut state, frame),
        "hover tick should keep running"
    );
    assert!(
        app.tick(&mut state, frame),
        "hover settle tick should keep running"
    );
    app.inject_mouse_button(MouseButton::Left, true);
    assert!(
        app.tick(&mut state, frame),
        "press tick should keep running"
    );
    app.inject_mouse_move(right_x, cy);
    common::tick_n(&mut app, &mut state, frame, 3, "drag");
    app.inject_mouse_button(MouseButton::Left, false);
    assert!(
        app.tick(&mut state, frame),
        "release tick should keep running"
    );

    assert!(
        state.value > initial,
        "dragging the vertical logarithmic fader right should raise the bound value (was {initial}, now {})",
        state.value
    );
}
