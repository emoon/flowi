//! [`element_id`] hashes a (name, seed) pair without a frame arena, and a LayoutId kept from an earlier
//! frame still names the right element when handed to the core in a later one.

use flowi::{
    col, element_id, element_state, element_state_id, fixed, nav_frame, set_focus_id, Application,
    ElementId, LayoutId, UiInput,
};

mod common;

// Declaring the area a scroll target registers it in the interaction state.
const REGION_ID: &str = "oof_region";
const SEED: i32 = 7;
const ESCAPED_ID: &str = "oof_escaped_id_that_outlives_its_frame";
const JUNK_ID: &str = "oof_junk_id_overwriting_the_arena_xxxxx";
const REUSE_TICK: u32 = 2;
const ARENA_CHURN: usize = 64;
const W: f32 = 80.0;
const H: f32 = 40.0;

struct Probe {
    in_frame_id: ElementId,
    in_frame_found: bool,
    in_frame_size: (f32, f32),
    round_tripped_name: String,
    round_tripped_seed: i32,
    ticks: u32,
    escaped: Option<LayoutId>,
    kept: Option<LayoutId>,
    stale_found: Option<bool>,
}

impl Default for Probe {
    fn default() -> Self {
        Probe {
            in_frame_id: ElementId::INVALID,
            in_frame_found: false,
            in_frame_size: (0.0, 0.0),
            round_tripped_name: String::new(),
            round_tripped_seed: 0,
            ticks: 0,
            escaped: None,
            kept: None,
            stale_found: None,
        }
    }
}

fn frame(p: &mut Probe) {
    col! { ESCAPED_ID, width: fixed!(W), height: fixed!(H), color: [60, 40, 40] {
        UiInput::scroll_target(ESCAPED_ID);
    }}

    if p.ticks == 0 {
        p.escaped = Some(LayoutId::from(ESCAPED_ID));
        p.kept = Some(LayoutId::from(ESCAPED_ID.to_string()));
    } else if p.ticks == REUSE_TICK {
        // Overwrite the arena strings of the earlier frames, then hand the old id to the core.
        for _ in 0..ARENA_CHURN {
            element_state(JUNK_ID);
        }
        let stale = p.escaped.take().expect("tick 0 stashed an id");
        p.stale_found = Some(element_state(stale).found);
    }
    p.ticks += 1;

    col! { (REGION_ID, SEED), width: fixed!(W), height: fixed!(H), color: [40, 40, 60] {
        UiInput::scroll_target((REGION_ID, SEED));
    }}

    let id = LayoutId::from((REGION_ID, SEED));
    p.round_tripped_name = id.name().to_string();
    p.round_tripped_seed = id.seed();
    p.in_frame_id = id.hashed_id();

    let state = element_state((REGION_ID, SEED));
    p.in_frame_found = state.found;
    p.in_frame_size = (state.size.x, state.size.y);
}

#[test]
fn element_id_and_state_read_the_same_element_outside_a_frame() {
    let mut app = Application::new(200, 120, true);
    let mut probe = Probe::default();

    // The rect is the previous frame's resolved layout, so settle before reading.
    app.inject_mouse_move(150, 100);
    common::tick_n(&mut app, &mut probe, frame, 4, "settle");
    assert!(
        probe.in_frame_found,
        "the scroll-target region should be registered by now"
    );

    // Everything below runs between ticks, with no frame open.
    assert_eq!(
        probe.round_tripped_name, REGION_ID,
        "LayoutId::name reports the name it was built from"
    );
    assert_eq!(
        probe.round_tripped_seed, SEED,
        "LayoutId::seed reports the seed it was built from"
    );

    let out_of_frame = element_id(REGION_ID, SEED);
    assert_eq!(
        out_of_frame, probe.in_frame_id,
        "the frame-free hash must equal the one LayoutId::hashed_id produces in frame"
    );
    assert_ne!(
        out_of_frame,
        element_id(REGION_ID, SEED + 1),
        "a different seed must hash to a different element"
    );

    let state = element_state_id(out_of_frame);
    assert!(
        state.found,
        "reading by ElementId outside a frame finds the element laid out in the last frame"
    );
    assert_eq!(
        (state.size.x, state.size.y),
        probe.in_frame_size,
        "the by-id read is normalised to logical px exactly as the in-frame read is"
    );
    assert!(
        !element_state_id(element_id("oof_never_registered", 0)).found,
        "a never-registered id reports found = false"
    );

    set_focus_id(out_of_frame);
    // The nav snapshot is taken when a frame opens, so the focus set between ticks shows up in the next one.
    assert!(
        app.tick(&mut probe, frame),
        "focus tick should keep running"
    );
    assert_eq!(
        nav_frame().focused_id,
        out_of_frame.0,
        "set_focus_id lands the frame-free id in the nav snapshot"
    );

    // A LayoutId stashed during a frame stays readable after its arena is reused and its application is gone.
    assert_eq!(
        probe.stale_found,
        Some(true),
        "an id kept from an earlier frame must still find its element when handed to the core later"
    );
    let escaped = probe.kept.take().expect("tick 0 stashed an id");
    assert_eq!(
        escaped.name(),
        ESCAPED_ID,
        "name() must not read the expired frame arena"
    );
    drop(app);
    assert_eq!(
        escaped.name(),
        ESCAPED_ID,
        "name() must stay readable after the application is gone"
    );
    assert_eq!(
        escaped.hashed_id(),
        element_id(ESCAPED_ID, 0),
        "hashed_id() must equal the frame-free hash after the frame and application are gone"
    );
}
