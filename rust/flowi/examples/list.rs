//! A minimal List demo: a scrollable, virtualized column of caller-emitted rows driven through the scoped
//! [`List`] face, with the List's built-in multi-selection and drag-reorder. Each row carries an opaque u64
//! stable id (here the id at its position); the List runs the selection gestures internally - plain click
//! replaces, ctrl/⌘-click toggles, shift-click ranges from the anchor, Ctrl+A selects all - auto-applying them
//! into the caller-owned [`SelectionSet`] and drawing the row highlight from set membership. Dragging a row past
//! the threshold begins a reorder: the List draws the ghost + insertion line, and on release returns a
//! [`Reorder`] request the app applies to its own ordered model (here the ordered Vec). Only the rows in the
//! visible window are emitted; the rest never lay out, yet Ctrl+A still selects them and a reorder still
//! resolves their stable ids because the List forwards the full ordered slice.
//! It needs a display; run it and click / shift-click / ctrl-click / drag-reorder rows:
//!
//! ```text
//! cargo run -p flowi --example list
//! ```

use flowi::{
    col, grow, row, Application, DropRelationship, List, ListConfig, Reorder, SelectionSet,
};

const ROW_COUNT: u32 = 200;

// The List only describes the drop; applying it to the ordered model is the app's job.
fn apply_reorder(ordered: &mut Vec<u64>, r: &Reorder) {
    let moving: Vec<u64> = ordered
        .iter()
        .copied()
        .filter(|id| r.from_ids.contains(id))
        .collect();
    ordered.retain(|id| !r.from_ids.contains(id));
    let insert_at = match ordered.iter().position(|&id| id == r.target) {
        Some(p) => match r.rel {
            DropRelationship::Before => p,
            DropRelationship::After => p + 1,
            DropRelationship::Into => p + 1, // a List never produces Into; treat as After
        },
        None => ordered.len(), // target was itself dragged or is absent - append to the end
    };
    for (k, id) in moving.into_iter().enumerate() {
        ordered.insert(insert_at + k, id);
    }
}

fn main() {
    let app = Application::new(800, 600, false);
    // The framework keeps only the gesture anchor; the resolved selection and the row order are caller-owned.
    let mut ordered: Vec<u64> = (1..=ROW_COUNT as u64).collect();
    app.run(SelectionSet::new(), move |selection: &mut SelectionSet| {
        col! { width: grow!(), height: grow!() {
            let range = List::begin("demo_list", &ordered, Some(selection),
                                    ListConfig { row_height: 28.0, gap: 4.0, ..Default::default() });
            for i in range.start..range.end {
                // Flat list: the id at this position is the stable id.
                let stable_id = ordered[i as usize];
                let sig = List::item_begin(stable_id);
                if sig.clicked {
                    println!("row {} clicked (stable id {}), {} selected", i, stable_id, selection.len());
                }
                // Transparent row content so the List's selection highlight (drawn on the row wrapper) shows
                // through.
                row! { width: grow!(), height: grow!() {} }
                List::item_end();
            }
            let result = List::end();
            // Already auto-applied into selection (we passed Some); reading them shows what changed.
            let requests = List::requests(&result);
            if !requests.is_empty() {
                println!("emitted {} selection request(s): {:?}", requests.len(), requests);
            }
            if let Some(reorder) = List::reorder(&result) {
                println!("reorder: move {:?} {:?} {}", reorder.from_ids, reorder.rel, reorder.target);
                apply_reorder(&mut ordered, &reorder);
            }
        }}
    });
}
