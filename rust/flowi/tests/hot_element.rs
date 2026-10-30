//! Both regions are scroll targets. An inner target shadows its ancestors, so hovering the overlap reports
//! the inner element.
//!
//! hashed_id() hashes the same (name, seed) each element lays out under, so an id hashed here equals the
//! hot_element() result when that element is hot. hashed_id() needs an active frame, so the map is built
//! inside the frame callback.

use std::collections::HashMap;

use flowi::{col, fixed, hot_element, Application, ElementId, LayoutId, UiInput};

mod common;

// The layout, the scroll-target registration and the hashed_id map key share one name per region.
const OUTER_ID: &str = "he_outer";
const INNER_ID: &str = "he_inner";
const OUTER: f32 = 100.0;
const INNER: f32 = 40.0;

#[derive(Default)]
struct Probe {
    payload: Option<&'static str>,
    hot_raw: Option<ElementId>,
    inner_hashed: Option<ElementId>,
    outer_hashed: Option<ElementId>,
}

// Default top-left alignment puts the outer region at the window's top-left and the inner at its top-left.
fn frame(p: &mut Probe) {
    col! { OUTER_ID, width: fixed!(OUTER), height: fixed!(OUTER), color: [30, 30, 40] {
        UiInput::scroll_target(OUTER_ID);
        col! { INNER_ID, width: fixed!(INNER), height: fixed!(INNER), color: [80, 80, 120] {
            UiInput::scroll_target(INNER_ID);
        }}
    }}

    // The hash is one-way, so map back by hashing our own ids and keying a map on the result.
    let outer_id = LayoutId::from(OUTER_ID).hashed_id();
    let inner_id = LayoutId::from(INNER_ID).hashed_id();
    let mut map: HashMap<ElementId, &'static str> = HashMap::new();
    map.insert(outer_id, "outer");
    map.insert(inner_id, "inner");

    let hot = hot_element();
    p.hot_raw = hot;
    p.inner_hashed = Some(inner_id);
    p.outer_hashed = Some(outer_id);
    p.payload = hot.and_then(|id| map.get(&id).copied());
}

#[test]
fn headless_hot_element_map_back_and_topmost() {
    let mut app = Application::new(200, 200, true);
    let mut p = Probe::default();

    // (150,150) parks the cursor outside the outer region. The rect is the previous frame's resolved layout,
    // so a few frames are needed before the hit-test is live.
    app.inject_mouse_move(150, 150);
    common::tick_n(&mut app, &mut p, frame, 4, "settle");
    assert!(
        p.hot_raw.is_none(),
        "nothing hovered => hot_element() is None"
    );
    assert_eq!(p.payload, None, "no payload recovered when nothing is hot");

    // (10,10) is inside both regions; the inner is topmost.
    app.inject_mouse_move(10, 10);
    common::tick_n(&mut app, &mut p, frame, 3, "inner-hover");
    assert_eq!(
        p.hot_raw, p.inner_hashed,
        "hashed_id(inner) equals the core's hot id for the hovered element"
    );
    assert_eq!(
        p.payload,
        Some("inner"),
        "map-back recovers the inner payload; the topmost element wins"
    );

    // (70,70) is outside the 40x40 inner region, so only the outer is under the cursor.
    app.inject_mouse_move(70, 70);
    common::tick_n(&mut app, &mut p, frame, 3, "outer-hover");
    assert_eq!(
        p.hot_raw, p.outer_hashed,
        "hashed_id(outer) equals the core's hot id for the hovered element"
    );
    assert_eq!(
        p.payload,
        Some("outer"),
        "cursor over the outer-only area recovers the outer payload"
    );

    app.inject_mouse_move(150, 150);
    common::tick_n(&mut app, &mut p, frame, 3, "un-hover");
    assert!(
        p.hot_raw.is_none(),
        "leaving every region clears hot to None"
    );
    assert_eq!(p.payload, None, "no payload once nothing is hot again");
}
