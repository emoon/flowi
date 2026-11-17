//! Tree views ([`Tree`]) over rows the app owns, and the value types they exchange ([`TreeConfig`],
//! [`TreeResult`], [`TreeToggle`]).

use crate::sys;

use crate::frame_thread::frame_thread_guard;
use crate::list::{frame_copy, frame_labels, set_handle};
use crate::{
    LayoutId, ListConfig, ListRange, ListResult, SelectionSet, Tree, TreeExpanded, TreeRow,
};

impl Tree {
    /// Starts a tree and returns the range of rows to draw.
    ///
    /// `rows` holds every visible row, top to bottom. Each id must be unique and not 0. Selection goes into
    /// `set`. With `None`, read the
    /// changes from [`List::requests`](crate::List::requests) on [`TreeResult::list`] and draw your own
    /// highlight.
    ///
    /// # Panics
    ///
    /// Panics if `rows` holds more than `u32::MAX` rows, or when called off the thread that owns the live
    /// [`Application`](crate::Application).
    pub fn begin(
        id: impl Into<LayoutId>,
        rows: &[TreeRow],
        set: Option<&mut SelectionSet>,
        config: TreeConfig<'_>,
    ) -> ListRange {
        let count = u32::try_from(rows.len()).expect("a Tree holds at most u32::MAX rows");
        let id: LayoutId = id.into();
        let _frame = frame_thread_guard("Tree::begin");
        let labels = if config.list.labels.len() == rows.len() {
            frame_labels(config.list.labels)
        } else {
            std::ptr::null()
        };
        // SAFETY: frame_copy copies rows into the frame arena, which outlives the Tree's begin..end scope, so
        // the pointer the Tree keeps until Tree::end never reaches the caller's slice. The set pointer is null
        // or the handle of the &mut SelectionSet borrowed here. config.to_sys carries only frame-arena
        // pointers, and _frame keeps the call on the frame context's thread.
        unsafe {
            sys::fl_tree_begin(
                id.into_sys(),
                frame_copy(rows),
                count,
                set_handle(set),
                config.to_sys(labels),
            )
        }
    }

    /// Starts a tree that can hold millions of rows, and returns the range of rows to draw.
    ///
    /// `root_count` is the number of top-level rows and `expanded` holds the expanded nodes in the order they
    /// appear on screen. For each row in the range, call [`Tree::row_at`] to find out which child it is, then
    /// [`Tree::item_begin_virtual`] and [`Tree::item_end`].
    ///
    /// Rows have no ids of their own. The id of a row is its row number plus one, in `set` and in the result.
    /// Type-ahead is off and [`ListConfig::labels`] is not read.
    ///
    /// Right can ask you to expand a row that has no children. Ignore that toggle.
    ///
    /// # Panics
    ///
    /// Panics if `expanded` holds more than `u32::MAX` nodes, or when called off the thread that owns the
    /// live [`Application`](crate::Application).
    pub fn begin_virtual(
        id: impl Into<LayoutId>,
        root_count: u32,
        expanded: &[TreeExpanded],
        set: Option<&mut SelectionSet>,
        config: TreeConfig<'_>,
    ) -> ListRange {
        let count =
            u32::try_from(expanded.len()).expect("a Tree holds at most u32::MAX expanded nodes");
        let id: LayoutId = id.into();
        let _frame = frame_thread_guard("Tree::begin_virtual");
        // SAFETY: frame_copy copies expanded into the frame arena, which outlives the Tree's begin..end
        // scope and Tree::row_at calls later in the frame. The set pointer is null or the handle of the &mut
        // SelectionSet borrowed here. The config carries no label pointer, and _frame keeps the call on the
        // frame context's thread.
        unsafe {
            sys::fl_tree_begin_virtual(
                id.into_sys(),
                root_count,
                frame_copy(expanded),
                count,
                set_handle(set),
                config.to_sys(std::ptr::null()),
            )
        }
    }

    /// Ends the tree and returns the result for this frame.
    ///
    /// # Panics
    ///
    /// Panics when called off the thread that owns the live [`Application`](crate::Application).
    pub fn end() -> TreeResult {
        let _frame = frame_thread_guard("Tree::end");
        // SAFETY: a plain call that closes the Tree opened by begin; the list result's pointers are read,
        // and copied out, by from_sys before any further Tree or List call.
        TreeResult::from_sys(unsafe { sys::fl_tree_end() })
    }
}

/// Options for [`Tree::begin`] and [`Tree::begin_virtual`].
///
/// [`Default`] uses the Tree's own row height, gap and indent.
#[derive(Copy, Clone, Debug, Default)]
pub struct TreeConfig<'a> {
    /// The row height, gap, scrollbar and type-ahead labels, as for a List.
    ///
    /// Type-ahead needs one label for each row passed to [`Tree::begin`]. With any other number of labels it
    /// is off.
    pub list: ListConfig<'a>,
    /// The indent of each nesting level, in logical pixels (0 uses 16). The arrow is this wide too.
    pub indent: f32,
}

impl TreeConfig<'_> {
    fn to_sys(self, labels: *const sys::RawStr) -> sys::TreeConfig {
        sys::TreeConfig {
            list: self.list.to_sys(labels),
            indent: self.indent,
        }
    }
}

/// A request to expand or collapse a row, from [`TreeResult::toggle`].
///
/// The Tree does not remember which rows are expanded. Change your own data and pass the new rows next frame.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct TreeToggle {
    /// The id of the row.
    pub id: u64,
    /// The row number of that row this frame.
    pub row: u32,
    /// Whether the row should expand (true) or collapse (false).
    pub expanded: bool,
}

/// The outcome of a tree for one frame, returned by [`Tree::end`].
#[derive(Clone, Debug, PartialEq)]
pub struct TreeResult {
    list: ListResult,
    toggle: Option<TreeToggle>,
}

impl TreeResult {
    fn from_sys(raw: sys::TreeResult) -> Self {
        let toggle = (raw.toggled_id != 0).then_some(TreeToggle {
            id: raw.toggled_id,
            row: raw.toggled_row,
            expanded: raw.toggle_expanded,
        });
        TreeResult {
            list: ListResult::from_sys(raw.list),
            toggle,
        }
    }

    /// The selection, activation and focus, as for a List. Rows cannot be dragged, so there is never a
    /// reorder.
    pub fn list(&self) -> &ListResult {
        &self.list
    }

    /// The row the user asked to expand or collapse, or `None`.
    pub fn toggle(&self) -> Option<TreeToggle> {
        self.toggle
    }
}
