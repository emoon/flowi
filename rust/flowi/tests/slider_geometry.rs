//! SliderConfig's track_height and thumb_size reach the renderer.
//!
//! The two fields need different observations. thumb_size is the height of the track row, which lays out under
//! the widget id, so element_state reads it back directly. track_height only sizes the filled/unfilled child
//! rects, and those carry no id - so it is measured off the rendered frame instead, scanning a column through the
//! track and counting the rows the bar actually paints.

use flowi::{
    col, element_state, grow, slider_float_with, Application, PixelView, SliderConfig, WidgetConfig,
};

mod common;

// The id the slider lays its track row out under.
const TRACK_ID: &str = "slider_geometry_track";

// Column to scan, as an offset from the track row's left edge. At half value in a 400px-wide window the filled
// bar runs about 190px from that edge, so 40 is inside it, clear of the rounded left cap and of the thumb.
const SCAN_X: usize = 40;

#[derive(Default)]
struct Skin {
    /// Half, so the filled bar covers the scanned column.
    value: f32,
    /// 0.0 on either asks for the built-in default.
    track_height: f32,
    thumb_size: f32,
    /// Previous-frame track-row rect, captured inside the frame.
    row_x: f32,
    row_y: f32,
    row_h: f32,
    found: bool,
}

fn frame(s: &mut Skin) {
    col! { width: grow!(), height: grow!() {
        slider_float_with(
            "Cutoff",
            &mut s.value,
            0.0,
            1.0,
            SliderConfig {
                track_height: s.track_height,
                thumb_size: s.thumb_size,
                widget: WidgetConfig { id_override: TRACK_ID, ..Default::default() },
                ..Default::default()
            },
        );
        let st = element_state(TRACK_ID);
        s.found = st.found;
        s.row_x = st.pos.x;
        s.row_y = st.pos.y;
        s.row_h = st.size.y;
    }}
}

/// Rows the track bar paints at column x, measured within the track row's own extent.
///
/// The bar is centered in the row and thinner than it, so the row's top scanline is the container's bare
/// tint; anything differing from it down the row is the bar.
fn bar_thickness(pv: &PixelView, x: usize, row_y: usize, row_h: usize) -> usize {
    let px = |y: usize| {
        let i = (y * pv.width as usize + x) * pv.channels;
        &pv.pixels[i..i + pv.channels]
    };
    let tint = px(row_y).to_vec();
    (row_y..row_y + row_h)
        .filter(|&y| px(y) != tint.as_slice())
        .count()
}

#[test]
fn headless_slider_geometry_reaches_c() {
    let mut app = Application::new(400, 200, true);
    let mut state = Skin {
        value: 0.5,
        ..Default::default()
    };

    // The bar widths derive from the row width the slider carried over from the previous frame, so more than
    // one frame is needed.
    common::tick_n(&mut app, &mut state, frame, 4, "settle");
    assert!(
        state.found,
        "the track row should lay out under its `widget.id_override` (\"{TRACK_ID}\")"
    );
    assert!(
        (state.row_h - 24.0).abs() < 1e-3,
        "a zero `thumb_size` must keep the built-in 24px thumb (row height was {})",
        state.row_h
    );
    let pv = app.read_pixels().expect("render backend readback");
    let x = state.row_x as usize + SCAN_X;
    assert_eq!(
        bar_thickness(&pv, x, state.row_y as usize, state.row_h as usize),
        8,
        "a zero `track_height` must keep the built-in 8px bar"
    );

    // A dropped field leaves its measurement at the default.
    state.track_height = 3.0;
    state.thumb_size = 13.0;
    common::tick_n(&mut app, &mut state, frame, 4, "skinned");
    assert!(
        (state.row_h - 13.0).abs() < 1e-3,
        "`thumb_size` set from safe Rust must reach the C widget through `to_sys` (row height was {})",
        state.row_h
    );
    let pv = app.read_pixels().expect("render backend readback");
    let x = state.row_x as usize + SCAN_X;
    assert_eq!(
        bar_thickness(&pv, x, state.row_y as usize, state.row_h as usize),
        3,
        "`track_height` set from safe Rust must reach the C widget through `to_sys`"
    );
}
