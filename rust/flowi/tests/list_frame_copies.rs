//! The List retains the id slice and type-ahead labels it is handed from `List::begin` until `List::end`. The
//! caller's id vector is overwritten between the two calls here, and Ctrl+A - resolved against the id slice at
//! `List::end` - must still select the original ids.

use flowi::{
    col, grow, Application, Key, List, ListConfig, ListResult, SelectionRequest, SelectionSet,
};

mod common;

const ROWS: u64 = 5;
const JUNK: u64 = 0xDEAD_BEEF;

struct Probe {
    ids: Vec<u64>,
    set: SelectionSet,
    last: Option<ListResult>,
}

fn frame(p: &mut Probe) {
    col! { width: grow!(), height: grow!() {
        List::request_focus("frame_copies", 0);
        let labels = ["alpha", "beta", "gamma", "delta", "epsilon"];
        let range = List::begin(
            "frame_copies",
            &p.ids,
            Some(&mut p.set),
            ListConfig { row_height: 20.0, labels: &labels, ..Default::default() },
        );
        for i in range.start..range.end {
            List::item_begin(p.ids[i as usize]);
            List::item_end();
        }
        let original = p.ids.clone();
        p.ids.fill(JUNK);
        p.last = Some(List::end());
        p.ids = original;
    }}
}

#[test]
fn the_list_reads_its_own_copy_of_the_ids_after_the_callers_ids_are_overwritten() {
    let mut app = Application::new(200, 200, true);
    let mut probe = Probe {
        ids: (1..=ROWS).collect(),
        set: SelectionSet::new(),
        last: None,
    };

    common::tick_n(&mut app, &mut probe, frame, 4, "settle");
    assert!(probe.set.is_empty(), "nothing is selected before Ctrl+A");

    app.inject_key(Key::Left_Control, true);
    app.inject_key(Key::A, true);
    assert!(
        app.tick(&mut probe, frame),
        "select-all tick should keep running"
    );

    let result = probe.last.take().expect("the frame ended a list");
    assert_eq!(
        List::requests(&result),
        vec![SelectionRequest::SetAll(true)],
        "Ctrl+A emits one select-all request"
    );
    let mut selected = probe.set.ids();
    selected.sort_unstable();
    assert_eq!(
        selected,
        (1..=ROWS).collect::<Vec<u64>>(),
        "select-all resolves against the ids as they were at begin"
    );
}
