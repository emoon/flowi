//! The hand-written `Tree` wrappers: a press on an arrow comes back as a `TreeToggle`, the Tree reads its own
//! copy of the rows from `Tree::begin` until `Tree::end`, and a virtual tree's toggle resolves through
//! `Tree::row_at` after `Tree::end`.

use flowi::{
    col, grow, Application, Key, List, ListConfig, SelectionSet, Tree, TreeConfig, TreeExpanded,
    TreeRow, TreeToggle, TreeVirtualRow,
};

mod common;

const JUNK: u64 = 0xDEAD_BEEF;
const VIRTUAL_ROOTS: u32 = 2;
const LABELS: [&str; 3] = ["alpha", "beta", "gamma"];

const EXPANDED: [TreeExpanded; 3] = [
    TreeExpanded {
        parent: -1,
        child_index: 0,
        child_count: 2,
    },
    TreeExpanded {
        parent: 0,
        child_index: 0,
        child_count: 1,
    },
    TreeExpanded {
        parent: -1,
        child_index: 1,
        child_count: 1,
    },
];
const LAST_ROW: u32 = 5;

const ARROW_X: i32 = 8;
const INDENT: i32 = 16;
const ROW_STRIDE: i32 = 28;
const ROW_MID_Y: i32 = 10;

struct Probe {
    rows: Vec<TreeRow>,
    set: SelectionSet,
    overwrite_rows: bool,
    toggles: Vec<TreeToggle>,
    toggled_rows: Vec<TreeVirtualRow>,
}

impl Probe {
    fn new() -> Self {
        let rows = (1..=3)
            .map(|id| TreeRow {
                id,
                depth: 0,
                has_children: id == 1,
                expanded: false,
            })
            .collect();
        Probe {
            rows,
            set: SelectionSet::new(),
            overwrite_rows: false,
            toggles: Vec::new(),
            toggled_rows: Vec::new(),
        }
    }
}

fn config(labels: &'static [&'static str]) -> TreeConfig<'static> {
    TreeConfig {
        list: ListConfig {
            row_height: 20.0,
            labels,
            ..Default::default()
        },
        ..Default::default()
    }
}

fn flat_frame(p: &mut Probe) {
    col! { width: grow!(), height: grow!() {
        List::request_focus("tree", 0);
        let range = Tree::begin("tree", &p.rows, Some(&mut p.set), config(&LABELS));
        let original = p.rows.clone();
        if p.overwrite_rows {
            p.rows.fill(TreeRow { id: JUNK, depth: 9, has_children: false, expanded: false });
        }
        for row in range.start..range.end {
            Tree::item_begin(row);
            Tree::item_end();
        }
        p.toggles.extend(Tree::end().toggle());
        p.rows = original;
    }}
}

fn virtual_frame(p: &mut Probe) {
    col! { width: grow!(), height: grow!() {
        let range = Tree::begin_virtual("virtual_tree", VIRTUAL_ROOTS, &EXPANDED, Some(&mut p.set), config(&[]));
        for row in range.start..range.end {
            Tree::item_begin_virtual(row, true);
            Tree::item_end();
        }
        if let Some(toggle) = Tree::end().toggle() {
            p.toggles.push(toggle);
            p.toggled_rows.push(Tree::row_at(toggle.row));
        }
    }}
}

fn a_press_on_the_arrow_reports_one_toggle(app: &mut Application) {
    let mut probe = Probe::new();

    common::tick_n(app, &mut probe, flat_frame, 4, "settle");
    assert!(probe.toggles.is_empty(), "no toggle before any input");

    common::click_at(app, &mut probe, flat_frame, ARROW_X, ROW_MID_Y);
    common::tick_n(app, &mut probe, flat_frame, 2, "observe");

    assert_eq!(
        probe.toggles,
        vec![TreeToggle {
            id: 1,
            row: 0,
            expanded: true
        }],
        "one press on the collapsed row's arrow asks for one expand"
    );
}

fn the_tree_reads_its_own_copy_of_the_rows_after_the_callers_rows_are_overwritten(
    app: &mut Application,
) {
    let mut probe = Probe::new();
    probe.overwrite_rows = true;

    common::tick_n(app, &mut probe, flat_frame, 4, "settle");
    assert!(probe.set.is_empty(), "nothing is selected before Ctrl+A");

    app.inject_key(Key::Left_Control, true);
    app.inject_key(Key::A, true);
    common::tick_n(app, &mut probe, flat_frame, 1, "select-all");

    let mut selected = probe.set.ids();
    selected.sort_unstable();
    assert_eq!(
        selected,
        vec![1, 2, 3],
        "select-all resolves against the rows as they were at begin"
    );
}

fn a_virtual_toggle_resolves_through_row_at_after_end(app: &mut Application) {
    let mut probe = Probe::new();

    common::tick_n(app, &mut probe, virtual_frame, 4, "settle");
    let y = LAST_ROW as i32 * ROW_STRIDE + ROW_MID_Y;
    common::click_at(app, &mut probe, virtual_frame, INDENT + ARROW_X, y);

    assert_eq!(
        probe.toggles,
        vec![TreeToggle {
            id: u64::from(LAST_ROW) + 1,
            row: LAST_ROW,
            expanded: true
        }],
        "a virtual row's id is its row number plus one"
    );
    let row = probe.toggled_rows[0];
    assert_eq!(
        (row.parent, row.child_index, row.depth, row.node),
        (2, 0, 1, -1),
        "the last row is the only child of the third expanded node and is not expanded"
    );
}

#[test]
fn tree_wrappers() {
    let mut app = Application::new(200, 200, true);
    a_press_on_the_arrow_reports_one_toggle(&mut app);
    a_virtual_toggle_resolves_through_row_at_after_end(&mut app);
    the_tree_reads_its_own_copy_of_the_rows_after_the_callers_rows_are_overwritten(&mut app);
}
