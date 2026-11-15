//! A small tree view: the app owns the nodes and which of them are expanded, and hands the [`Tree`] the rows
//! that are visible this frame. Click an arrow, or press Right and Left, to expand and collapse.
//!
//! ```text
//! cargo run -p flowi --example tree
//! ```

use flowi::{
    col, grow, text_styled, Application, Color, ListConfig, SelectionSet, TextConfig, Tree,
    TreeConfig, TreeRow,
};

const NODES: [(&str, u16); 11] = [
    ("src", 0),
    ("ui", 1),
    ("list.c", 2),
    ("tree.c", 2),
    ("render", 1),
    ("painter.c", 2),
    ("main.c", 1),
    ("docs", 0),
    ("adr", 1),
    ("0001-layout.md", 2),
    ("README.md", 0),
];

const TEXT: Color = Color {
    r: 220,
    g: 220,
    b: 220,
    a: 255,
};

struct State {
    expanded: [bool; NODES.len()],
    selection: SelectionSet,
}

fn visible_rows(expanded: &[bool]) -> Vec<TreeRow> {
    let mut rows = Vec::new();
    let mut hidden_below = u16::MAX;
    for (i, &(_, depth)) in NODES.iter().enumerate() {
        if depth > hidden_below {
            continue;
        }
        let has_children = NODES.get(i + 1).is_some_and(|next| next.1 > depth);
        hidden_below = if has_children && !expanded[i] {
            depth
        } else {
            u16::MAX
        };
        rows.push(TreeRow {
            id: i as u64 + 1,
            depth,
            has_children,
            expanded: expanded[i],
        });
    }
    rows
}

fn frame(state: &mut State) {
    let rows = visible_rows(&state.expanded);
    let labels: Vec<&str> = rows.iter().map(|r| NODES[r.id as usize - 1].0).collect();
    let config = TreeConfig {
        list: ListConfig {
            row_height: 28.0,
            gap: 2.0,
            labels: &labels,
            ..Default::default()
        },
        ..Default::default()
    };
    col! { width: grow!(), height: grow!() {
        let range = Tree::begin("demo_tree", &rows, Some(&mut state.selection), config);
        for row in range.start..range.end {
            Tree::item_begin(row);
            text_styled(labels[row as usize], TextConfig { color: TEXT, ..Default::default() });
            Tree::item_end();
        }
        if let Some(toggle) = Tree::end().toggle() {
            state.expanded[toggle.id as usize - 1] = toggle.expanded;
        }
    }}
}

fn main() {
    let app = Application::new(800, 600, false);
    let mut expanded = [false; NODES.len()];
    expanded[0] = true;
    let state = State {
        expanded,
        selection: SelectionSet::new(),
    };
    app.run(state, frame);
}
