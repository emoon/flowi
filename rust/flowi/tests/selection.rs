//! SelectionSet is the opaque, RAII handle over `selection_set_*`. It is self-contained - backed
//! by its own arena, it needs no live Application and no frame.

use flowi::{SelectionRequest, SelectionSet};

#[test]
fn membership_and_set_all_round_trip() {
    let mut sel = SelectionSet::new();
    assert!(sel.is_empty());
    assert!(!sel.contains(42));

    sel.set(42, true);
    assert!(sel.contains(42));
    assert_eq!(sel.len(), 1);

    sel.set(42, false);
    assert!(!sel.contains(42));

    sel.set_all(&[10, 20, 30]);
    assert_eq!(sel.len(), 3);
    assert!(sel.contains(20));

    let mut ids = sel.ids(); // order unspecified
    ids.sort_unstable();
    assert_eq!(ids, vec![10, 20, 30]);

    sel.clear();
    assert!(sel.is_empty());
}

#[test]
fn apply_requests_without_enumeration() {
    let mut sel = SelectionSet::new();
    // SetOne / ClearAll never need the ordered list; sequence is semantics (ClearAll then SetOne nets to {7}).
    sel.apply(
        &[
            SelectionRequest::SetOne(5, true),
            SelectionRequest::SetOne(6, true),
            SelectionRequest::ClearAll,
            SelectionRequest::SetOne(7, true),
        ],
        &[],
    );
    assert_eq!(sel.len(), 1);
    assert!(sel.contains(7));
}

#[test]
fn apply_range_spans_offscreen_ids() {
    let mut sel = SelectionSet::new();
    // apply walks this slice, not the visible window, so a range also selects off-screen ids.
    let model = [10u64, 20, 30, 40, 50];
    sel.apply(&[SelectionRequest::SetRange(20, 40, true)], &model);
    assert!(!sel.contains(10));
    assert!(sel.contains(20));
    assert!(sel.contains(30));
    assert!(sel.contains(40));
    assert!(!sel.contains(50));
    assert_eq!(sel.len(), 3);

    // Direction-agnostic: reversed endpoints yield the same span, here used to deselect.
    sel.apply(&[SelectionRequest::SetRange(40, 20, false)], &model);
    assert!(sel.is_empty());
}

#[test]
fn apply_set_all_resolves_from_ordered_ids() {
    let mut sel = SelectionSet::new();
    let model = [11u64, 22, 33];
    sel.apply(&[SelectionRequest::SetAll(true)], &model);
    assert_eq!(sel.len(), 3);
    sel.apply(&[SelectionRequest::SetAll(false)], &model);
    assert!(sel.is_empty());
}
