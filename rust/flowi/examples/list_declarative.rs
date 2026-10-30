//! The declarative List in one call: [`List::show`] takes an array of [`ListItem`] descriptors
//! ({ id, label, icon }) plus the caller-owned [`SelectionSet`] and runs the whole interaction internally -
//! virtualization, the multi-selection gesture machine, keyboard nav, marquee, drag-reorder, the highlight /
//! ghost / insertion-line drawing, and auto-applying selection - while rendering each row's label + icon. The
//! app reads only the policy outcomes off the [`ListResult`](flowi::ListResult): activated_id (open), context_menu_id
//! (right-click; the app opens its own menu), and the reorder request. Contrast examples/list.rs, which drives
//! the scoped face for rows with custom content. It needs a display; run it and click / shift-click /
//! ctrl-click / right-click / drag-reorder rows:
//!
//! ```text
//! cargo run -p flowi --example list_declarative
//! ```

use flowi::{
    col, grow, Application, DropRelationship, List, ListItem, ListOpts, Reorder, SelectionSet,
};

// The List only describes the drop; applying it to the model is the app's job.
fn apply_reorder(model: &mut Vec<(u64, String)>, r: &Reorder) {
    let moving: Vec<(u64, String)> = model
        .iter()
        .filter(|(id, _)| r.from_ids.contains(id))
        .cloned()
        .collect();
    model.retain(|(id, _)| !r.from_ids.contains(id));
    let insert_at = match model.iter().position(|(id, _)| *id == r.target) {
        Some(p) => match r.rel {
            DropRelationship::Before => p,
            DropRelationship::After => p + 1,
            DropRelationship::Into => p + 1, // a List never produces Into; treat as After
        },
        None => model.len(), // target was itself dragged or is absent - append
    };
    for (k, row) in moving.into_iter().enumerate() {
        model.insert(insert_at + k, row);
    }
}

fn main() {
    let app = Application::new(800, 600, false);
    // The List keeps only the gesture anchor; the ordered model and the selection set are caller-owned.
    let mut model: Vec<(u64, String)> =
        (1..=200u64).map(|id| (id, format!("Track {id}"))).collect();
    app.run(SelectionSet::new(), move |selection: &mut SelectionSet| {
        col! { width: grow!(), height: grow!() {
            // Labels are borrowed from the owned model, so the slice is rebuilt each frame.
            let items: Vec<ListItem> =
                model.iter().map(|(id, label)| ListItem { id: *id, label, icon: None }).collect();
            let result = List::show(
                "decl_list",
                &items,
                Some(selection),
                ListOpts { row_height: 28.0, gap: 4.0, ..Default::default() },
            );
            if result.activated_id() != 0 {
                println!("activated (open) id {}", result.activated_id());
            }
            if let Some(id) = List::context_menu(&result) {
                println!("context-menu requested for id {id} — app would open its own menu here");
            }
            if let Some(reorder) = List::reorder(&result) {
                println!("reorder: move {:?} {:?} {}", reorder.from_ids, reorder.rel, reorder.target);
                apply_reorder(&mut model, &reorder);
            }
        }}
    });
}
