//! Scrolling lists ([`List`]), the [`SelectionSet`] they select into, and the value types they exchange
//! ([`ListItem`], [`ListOpts`], [`Reorder`], [`SelectionRequest`]).

use std::os::raw::c_char;
use std::ptr;

use crate::sys;

use crate::frame_thread::frame_thread_guard;
use crate::{ensure_fl_init, DropRelationship, ImageHandle, LayoutId, List, ListRange};

impl List {
    /// Starts a list and returns the range of rows to draw.
    ///
    /// Call this inside a frame callback. `id` names the list, so give each list its own. `ordered_ids` holds
    /// the ids of all rows in display order. Each id must be unique and not 0.
    ///
    /// Selection goes into `set`. With `None`, read the changes from [`List::requests`] and draw your own
    /// highlight.
    ///
    /// # Panics
    ///
    /// Panics if `ordered_ids` holds more than `u32::MAX` ids, or when called off the thread that owns the live
    /// [`Application`](crate::Application).
    pub fn begin(
        id: impl Into<LayoutId>,
        ordered_ids: &[u64],
        set: Option<&mut SelectionSet>,
        config: ListConfig<'_>,
    ) -> ListRange {
        let set_ptr = set_handle(set);
        let count = u32::try_from(ordered_ids.len()).expect("a List holds at most u32::MAX ids");
        let id: LayoutId = id.into();
        let _frame = frame_thread_guard("List::begin");
        let labels = if config.labels.len() == ordered_ids.len() {
            frame_labels(config.labels)
        } else {
            ptr::null()
        };
        // SAFETY: frame_copy copies ordered_ids into the frame arena, which outlives the List's begin..end
        // scope, so the pointer the List retains until List::end never reaches the caller's slice; set_ptr is
        // either null or the handle of the &mut SelectionSet borrowed here, which the List may mutate for the
        // duration of the call and no longer. config.to_sys carries only frame-arena pointers, and _frame
        // keeps the call to the frame context's thread.
        unsafe {
            sys::fl_list_begin(
                id.into_sys(),
                frame_copy(ordered_ids),
                count,
                set_ptr,
                config.to_sys(labels),
            )
        }
    }

    /// Ends the list and returns the result for this frame.
    ///
    /// # Panics
    ///
    /// Panics when called off the thread that owns the live [`Application`](crate::Application).
    pub fn end() -> ListResult {
        let _frame = frame_thread_guard("List::end");
        // SAFETY: a plain call that closes the List opened by begin; the result's pointers are read, and
        // copied out, by from_sys before any further List call.
        ListResult::from_sys(unsafe { sys::fl_list_end() })
    }

    /// Returns the selection changes of this frame.
    ///
    /// They are already applied if you passed a set. Otherwise apply them yourself with
    /// [`SelectionSet::apply`] and the ids you passed to [`List::begin`].
    pub fn requests(result: &ListResult) -> Vec<SelectionRequest> {
        result.requests.clone()
    }

    /// Returns where the dragged rows were dropped this frame, or `None` if there was no drop.
    ///
    /// Move the rows in [`Reorder::from_ids`] to the [`Reorder::rel`] side of [`Reorder::target`] in your own
    /// data. The target can be one of the dragged rows.
    pub fn reorder(result: &ListResult) -> Option<Reorder> {
        result.reorder_emitted.then(|| result.reorder())
    }

    /// Returns where the rows being dragged would land, or `None` while no rows are dragged.
    ///
    /// Use it to draw the drag yourself when [`ListConfig::suppress_drag_visuals`] or
    /// [`ListOpts::suppress_drag_visuals`] is set. The rows have not moved yet, so leave your data alone.
    pub fn drag_state(result: &ListResult) -> Option<Reorder> {
        result.drag_active.then(|| result.reorder())
    }

    /// Shows a scrolling list of label rows and returns the result for this frame.
    ///
    /// Call this inside a frame callback. `id` names the list, so give each list its own. `items` holds the
    /// rows in display order. Selection goes into `set`. With `None`, no row is highlighted. To draw the rows
    /// yourself, use [`List::begin`].
    ///
    /// # Panics
    ///
    /// Panics if `items` holds more than `u32::MAX` rows, or when called off the thread that owns the live
    /// [`Application`](crate::Application).
    pub fn show(
        id: impl Into<LayoutId>,
        items: &[ListItem<'_>],
        set: Option<&mut SelectionSet>,
        opts: ListOpts,
    ) -> ListResult {
        let count = u32::try_from(items.len()).expect("a List holds at most u32::MAX items");
        let set_ptr = set_handle(set);
        let id: LayoutId = id.into();
        let _frame = frame_thread_guard("List::show");
        let sys_items: Vec<sys::ListItem> = items
            .iter()
            .map(|it| sys::ListItem {
                id: it.id,
                // SAFETY: the pointer/length pair describes the label's live bytes. fl_frame_str copies
                // them before returning, so the borrow in items only has to outlive this call; the arena
                // copy lives for the rest of the frame. It asserts an active frame, which this slot is
                // emitted inside.
                label: unsafe {
                    sys::fl_frame_str(it.label.as_ptr() as *const c_char, it.label.len())
                },
                icon: it.icon.unwrap_or(sys::ImageHandle::INVALID),
            })
            .collect();
        // SAFETY: sys_items is passed as a pointer/length pair over storage that outlives this synchronous
        // call; set_ptr is either null or the handle of the &mut SelectionSet borrowed here, mutable for
        // the duration of the call only.
        let raw = unsafe {
            sys::fl_input_list(
                id.into_sys(),
                sys_items.as_ptr(),
                count,
                set_ptr,
                opts.to_sys(),
            )
        };
        ListResult::from_sys(raw)
    }

    /// Returns the row the user right-clicked this frame, or `None`.
    ///
    /// The list opens no menu, so open yours for this row.
    pub fn context_menu(result: &ListResult) -> Option<u64> {
        (result.context_menu_id != 0).then_some(result.context_menu_id)
    }
}

/// A column of a list or tree with a header, passed in [`ListConfig::columns`].
///
/// You own the widths. When the user resizes a column, [`ListResult::resized`] reports the new width, and you pass
/// it back as `width` on the next frame.
#[derive(Copy, Clone, Debug, Default)]
pub struct Column<'a> {
    /// The id of the column (unique, never 0). Signals and results name a column by this id.
    pub id: u32,
    /// The text shown in the header.
    pub title: &'a str,
    /// The width of the column, in logical pixels (0 makes the column flexible).
    pub width: f32,
    /// The share of the leftover width a flexible column takes (0 uses 1).
    pub flex: f32,
    /// The narrowest the column can get, in logical pixels (0 for no minimum).
    ///
    /// If the columns do not fit, a flexible column stays at its minimum, a fixed one keeps its `width`, and the
    /// right edge is clipped.
    pub min_width: f32,
}

/// Options for [`List::begin`].
///
/// [`Default`] uses the List's own row height and gap, with type-ahead off.
#[derive(Copy, Clone, Debug)]
pub struct ListConfig<'a> {
    /// The height of every row, in logical pixels (0 uses 32).
    pub row_height: f32,
    /// The space between rows, in logical pixels (0 uses 8).
    pub gap: f32,
    /// The labels matched by type-ahead, one per id in the same order.
    ///
    /// Type-ahead is off when this is empty or its length differs from the number of ids.
    pub labels: &'a [&'a str],
    /// Whether you draw the row drag yourself, from [`List::drag_state`].
    pub suppress_drag_visuals: bool,
    /// When the scrollbar is shown ([`ScrollbarDisplayMode::Never`](crate::ScrollbarDisplayMode::Never) by
    /// default). The mouse wheel always scrolls.
    pub scrollbar_mode: sys::ScrollbarDisplayMode,
    /// The columns, or none for a list without a header.
    ///
    /// With columns, the list draws a header above the rows that stays in place while they scroll.
    pub columns: &'a [Column<'a>],
    /// The height of the header, in logical pixels (0 uses `row_height`).
    pub header_height: f32,
}

impl Default for ListConfig<'_> {
    fn default() -> Self {
        ListConfig {
            row_height: 0.0,
            gap: 0.0,
            labels: &[],
            suppress_drag_visuals: false,
            scrollbar_mode: sys::ScrollbarDisplayMode::Never,
            columns: &[],
            header_height: 0.0,
        }
    }
}

impl ListConfig<'_> {
    /// Lower to the `#[repr(C)]` mirror the C List consumes, with labels the frame-arena array already
    /// built from [`ListConfig::labels`] (null for none). Must run inside a frame, which copies the columns into
    /// the frame arena.
    pub(crate) fn to_sys(self, labels: *const sys::RawStr) -> sys::ListConfig {
        let column_count =
            u32::try_from(self.columns.len()).expect("a List holds at most u32::MAX columns");
        sys::ListConfig {
            row_height: self.row_height,
            gap: self.gap,
            labels,
            suppress_drag_visuals: self.suppress_drag_visuals,
            scrollbar_mode: self.scrollbar_mode,
            columns: frame_columns(self.columns),
            column_count,
            header_height: self.header_height,
        }
    }
}

/// The outcome of a list for one frame.
///
/// This is returned by [`List::end`] and [`List::show`]. The ids are the ones you passed in, and 0 means no
/// row. You can keep the value after the frame ends.
#[derive(Clone, Debug, PartialEq)]
pub struct ListResult {
    activated_id: u64,
    focused_id: u64,
    context_menu_id: u64,
    context_menu_cell: u32,
    requests: Vec<SelectionRequest>,
    nav_up_edge: bool,
    nav_down_edge: bool,
    drag_active: bool,
    reorder_emitted: bool,
    drag_from_ids: Vec<u64>,
    drag_target_id: u64,
    drag_rel: DropRelationship,
    header_clicked_column: u32,
    resized_column: u32,
    resized_width: f32,
    column_widths: Vec<f32>,
}

impl ListResult {
    /// Copy a C result and the frame storage it points at into an owned value.
    pub(crate) fn from_sys(raw: sys::ListResult) -> Self {
        let requests = if raw.requests.is_null() || raw.request_count == 0 {
            Vec::new()
        } else {
            // SAFETY: the C List wrote request_count valid mirror entries at requests this frame, and the
            // frame arena holding them is not rewound before this copy.
            let slice =
                unsafe { std::slice::from_raw_parts(raw.requests, raw.request_count as usize) };
            slice
                .iter()
                .map(|r| SelectionRequest::from_sys(*r))
                .collect()
        };
        let drag_from_ids = if raw.drag_from_ids.is_null() || raw.drag_from_count == 0 {
            Vec::new()
        } else {
            // SAFETY: the C List wrote drag_from_count valid u64s at drag_from_ids this frame, and the frame
            // arena holding them is not rewound before this copy.
            let slice = unsafe {
                std::slice::from_raw_parts(raw.drag_from_ids, raw.drag_from_count as usize)
            };
            slice.to_vec()
        };
        let column_widths = if raw.column_widths.is_null() || raw.column_width_count == 0 {
            Vec::new()
        } else {
            // SAFETY: the C List wrote column_width_count valid f32s at column_widths this frame, and the
            // frame arena holding them is not rewound before this copy.
            let slice = unsafe {
                std::slice::from_raw_parts(raw.column_widths, raw.column_width_count as usize)
            };
            slice.to_vec()
        };
        ListResult {
            activated_id: raw.activated_id,
            focused_id: raw.focused_id,
            context_menu_id: raw.context_menu_id,
            context_menu_cell: raw.context_menu_cell,
            requests,
            nav_up_edge: raw.nav_up_edge,
            nav_down_edge: raw.nav_down_edge,
            drag_active: raw.drag_active,
            reorder_emitted: raw.reorder_emitted,
            drag_from_ids,
            drag_target_id: raw.drag_target_id,
            drag_rel: raw.drag_rel,
            header_clicked_column: raw.header_clicked_column,
            resized_column: raw.resized_column,
            resized_width: raw.resized_width,
            column_widths,
        }
    }

    /// The drag payload as a [`Reorder`], whichever of committed or candidate it currently describes.
    fn reorder(&self) -> Reorder {
        Reorder {
            from_ids: self.drag_from_ids.clone(),
            target: self.drag_target_id,
            rel: self.drag_rel,
        }
    }

    /// The row the user activated with a double-click, Enter or Space (0 for none).
    pub fn activated_id(&self) -> u64 {
        self.activated_id
    }

    /// The row under the mouse, or under the keyboard cursor while the list has focus (0 for none).
    pub fn focused_id(&self) -> u64 {
        self.focused_id
    }

    /// The column the user right-clicked in the row that [`List::context_menu`] returns, or `None`.
    pub fn context_menu_cell(&self) -> Option<u32> {
        (self.context_menu_cell != 0).then_some(self.context_menu_cell)
    }

    /// The column whose header the user clicked, or `None`. The list does not sort.
    pub fn header_clicked(&self) -> Option<u32> {
        (self.header_clicked_column != 0).then_some(self.header_clicked_column)
    }

    /// The column the user is resizing and its new width in logical pixels, or `None`.
    ///
    /// It is reported on each frame of the drag, once the mouse has moved. Pass the width back as
    /// [`Column::width`] of that column, which also makes a flexible column fixed.
    pub fn resized(&self) -> Option<(u32, f32)> {
        (self.resized_column != 0).then_some((self.resized_column, self.resized_width))
    }

    /// The width every column got this frame, in logical pixels, in the order of [`ListConfig::columns`].
    ///
    /// Read it to save the layout. It is empty on the first frames of a list, until the list knows how wide it
    /// is.
    pub fn column_widths(&self) -> &[f32] {
        &self.column_widths
    }

    /// Whether the user pressed Up on the first row. Use it to move focus above the list.
    pub fn nav_up_edge(&self) -> bool {
        self.nav_up_edge
    }

    /// Whether the user pressed Down on the last row. Use it to move focus below the list.
    pub fn nav_down_edge(&self) -> bool {
        self.nav_down_edge
    }
}

/// Copy columns into the frame arena as a `ColumnDef` array, with each title copied alongside; null when there
/// are none. Must run inside a frame.
fn frame_columns(columns: &[Column<'_>]) -> *const sys::ColumnDef {
    if columns.is_empty() {
        return ptr::null();
    }
    let defs: Vec<sys::ColumnDef> = columns
        .iter()
        .map(|c| sys::ColumnDef {
            id: c.id,
            // SAFETY: the pointer/length pair describes the title's live bytes. fl_frame_str copies them
            // before returning, so the arena copy outlives the borrow. It asserts an active frame, which
            // every caller runs inside.
            title: unsafe { sys::fl_frame_str(c.title.as_ptr() as *const c_char, c.title.len()) },
            width: c.width,
            flex: c.flex,
            min_width: c.min_width,
        })
        .collect();
    frame_copy(&defs)
}

/// Copy labels into the frame arena as an FlString array and return it; null when labels is empty or too
/// long to count in a u32. Must run inside a frame.
pub(crate) fn frame_labels(labels: &[&str]) -> *const sys::RawStr {
    let Ok(count) = u32::try_from(labels.len()) else {
        return ptr::null();
    };
    // SAFETY: fl_frame_str_array reserves count slots (null for 0) and every slot is written before the
    // pointer is returned; fl_frame_str copies each string's bytes into the same frame arena, so the array
    // and its strings stay valid until the frame ends. Both assert an active frame, which List::begin runs in.
    unsafe {
        let slots = sys::fl_frame_str_array(count);
        for (i, s) in labels.iter().enumerate() {
            *slots.add(i) = sys::fl_frame_str(s.as_ptr() as *const c_char, s.len());
        }
        slots as *const sys::RawStr
    }
}

/// Copy items into the frame arena and return the copy; an empty slice yields its own dangling pointer, which
/// a zero count never reads. Must run inside a frame.
pub(crate) fn frame_copy<T: Copy>(items: &[T]) -> *const T {
    const {
        assert!(std::mem::align_of::<T>() <= std::mem::align_of::<sys::RawStr>());
    }
    if items.is_empty() {
        return items.as_ptr();
    }
    let slot = std::mem::size_of::<sys::RawStr>();
    let slots = u32::try_from(std::mem::size_of_val(items).div_ceil(slot))
        .expect("a frame copy holds at most u32::MAX slots");
    // SAFETY: fl_frame_str_array returns slots FlString-sized slots of frame-arena storage aligned for an
    // FlString, which the assertion above makes enough for a T; the slot count covers the bytes of items, and
    // the copy fills the first items.len() values. The storage stays valid until the frame ends. It asserts
    // an active frame, which the callers run in.
    unsafe {
        let dst = sys::fl_frame_str_array(slots) as *mut T;
        ptr::copy_nonoverlapping(items.as_ptr(), dst, items.len());
        dst
    }
}

pub(crate) fn set_handle(set: Option<&mut SelectionSet>) -> *mut sys::SelectionSet {
    match set {
        Some(s) => s.handle,
        None => ptr::null_mut(),
    }
}

/// A row of a [`List::show`] list.
#[derive(Copy, Clone, Debug)]
pub struct ListItem<'a> {
    /// The id of the row (unique, never 0).
    pub id: u64,
    /// The text shown on the row, also matched by type-ahead.
    pub label: &'a str,
    /// The icon drawn before the label, or `None` for no icon. An icon that is not ready is not drawn.
    pub icon: Option<ImageHandle>,
}

/// Options for [`List::show`].
///
/// [`Default`] turns `multi_select` and `reorderable` on and uses the List's own row height and gap.
#[derive(Copy, Clone, Debug)]
pub struct ListOpts {
    /// The height of every row, in logical pixels (0 uses 32).
    pub row_height: f32,
    /// The space between rows, in logical pixels (0 uses 8).
    pub gap: f32,
    /// Whether the user can select several rows (true by default). When false, a click selects one row and
    /// unselects the rest.
    pub multi_select: bool,
    /// Whether the user can drag rows to a new position (true by default). Read the drop from
    /// [`List::reorder`].
    pub reorderable: bool,
    /// Whether you draw the row drag yourself, from [`List::drag_state`].
    pub suppress_drag_visuals: bool,
    /// When the scrollbar is shown ([`ScrollbarDisplayMode::Never`](crate::ScrollbarDisplayMode::Never) by
    /// default). The mouse wheel always scrolls.
    pub scrollbar_mode: sys::ScrollbarDisplayMode,
}

impl Default for ListOpts {
    fn default() -> Self {
        ListOpts {
            row_height: 0.0,
            gap: 0.0,
            multi_select: true,
            reorderable: true,
            suppress_drag_visuals: false,
            scrollbar_mode: sys::ScrollbarDisplayMode::Never,
        }
    }
}

impl ListOpts {
    /// Lower to the `#[repr(C)]` mirror the C List consumes.
    fn to_sys(self) -> sys::ListOpts {
        sys::ListOpts {
            row_height: self.row_height,
            gap: self.gap,
            multi_select: self.multi_select,
            reorderable: self.reorderable,
            suppress_drag_visuals: self.suppress_drag_visuals,
            scrollbar_mode: self.scrollbar_mode,
        }
    }
}

/// Where dragged rows land, from [`List::reorder`] and [`List::drag_state`].
///
/// When it comes from [`List::reorder`], move the rows in `from_ids` to the `rel` side of `target` in your
/// own data.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Reorder {
    /// The dragged rows, in no particular order.
    ///
    /// This is every id in your set if the drag started on a selected row, and only the pressed row otherwise.
    pub from_ids: Vec<u64>,
    /// The row the drop is next to.
    pub target: u64,
    /// The side of `target` the rows land on. A List only reports [`DropRelationship::Before`] and
    /// [`DropRelationship::After`].
    pub rel: DropRelationship,
}

/// A selection change from a [`List`], returned by [`List::requests`].
///
/// Apply it to a [`SelectionSet`] with [`SelectionSet::apply`].
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum SelectionRequest {
    /// Unselects every row.
    ClearAll,
    /// Selects (true) or unselects (false) every row.
    SetAll(bool),
    /// Selects or unselects the row with this id.
    SetOne(u64, bool),
    /// Selects or unselects the rows from one id to the other, both included. The two ids can come in either
    /// order.
    SetRange(u64, u64, bool),
}

impl SelectionRequest {
    /// Lower to the flat `#[repr(C)]` mirror the C apply consumes.
    fn to_sys(self) -> sys::SelectionRequest {
        use sys::SelectionRequestKind as K;
        match self {
            SelectionRequest::ClearAll => sys::SelectionRequest {
                kind: K::ClearAll,
                selected: false,
                id: 0,
                to_id: 0,
            },
            SelectionRequest::SetAll(sel) => sys::SelectionRequest {
                kind: K::SetAll,
                selected: sel,
                id: 0,
                to_id: 0,
            },
            SelectionRequest::SetOne(id, sel) => sys::SelectionRequest {
                kind: K::SetOne,
                selected: sel,
                id,
                to_id: 0,
            },
            SelectionRequest::SetRange(from, to, sel) => sys::SelectionRequest {
                kind: K::SetRange,
                selected: sel,
                id: from,
                to_id: to,
            },
        }
    }

    /// Lift a flat C mirror (as emitted by the List on its [`ListResult`]) into this enum, dropping
    /// the fields each variant does not read.
    fn from_sys(r: sys::SelectionRequest) -> Self {
        use sys::SelectionRequestKind as K;
        match r.kind {
            K::ClearAll => SelectionRequest::ClearAll,
            K::SetAll => SelectionRequest::SetAll(r.selected),
            K::SetOne => SelectionRequest::SetOne(r.id, r.selected),
            K::SetRange => SelectionRequest::SetRange(r.id, r.to_id, r.selected),
        }
    }
}

/// The ids of the selected rows of a [`List`].
///
/// You own the set and pass it to the list each frame. Read it with [`contains`](SelectionSet::contains)
/// and change it with [`set`](SelectionSet::set), [`set_all`](SelectionSet::set_all),
/// [`clear`](SelectionSet::clear) or [`apply`](SelectionSet::apply).
pub use crate::generated::ui_selection::SelectionSet;

impl SelectionSet {
    /// Creates an empty set.
    ///
    /// You can create one before the [`Application`](crate::Application) exists.
    pub fn new() -> Self {
        Self::with_capacity(0)
    }

    /// Creates an empty set with room for `capacity` ids. The set grows when it needs more.
    ///
    /// # Panics
    ///
    /// Panics if the set cannot be allocated.
    pub fn with_capacity(capacity: u32) -> Self {
        ensure_fl_init();
        // SAFETY: a plain call over a u32; the base subsystems it allocates from are up (just ensured).
        let handle = unsafe { sys::fl_selection_set_create(capacity) };
        assert!(!handle.is_null(), "selection_set_create failed");
        SelectionSet { handle }
    }

    /// Applies `requests` to the set, in order.
    ///
    /// `ordered_ids` holds the ids of all rows in display order. [`SelectionRequest::SetRange`] and
    /// [`SelectionRequest::SetAll`] need it to find their rows. It can be empty when neither is in `requests`.
    pub fn apply(&mut self, requests: &[SelectionRequest], ordered_ids: &[u64]) {
        let lowered: Vec<sys::SelectionRequest> = requests.iter().map(|r| r.to_sys()).collect();
        // SAFETY: self.handle is the live handle this set owns, and both slices are passed as
        // pointer/length pairs over storage that outlives the synchronous call, and nothing escapes it.
        unsafe {
            sys::fl_selection_set_apply(
                self.handle,
                lowered.as_ptr(),
                lowered.len() as u64,
                ordered_ids.as_ptr(),
                ordered_ids.len() as u64,
            )
        };
    }

    /// Returns the number of selected ids.
    #[inline]
    pub fn len(&self) -> usize {
        // SAFETY: self.handle is the live handle this set owns.
        unsafe { sys::fl_selection_set_len(self.handle) as usize }
    }

    /// Returns true if nothing is selected.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns the selected ids, in no particular order.
    ///
    /// To fill a slice of your own instead, use [`copy_ids`](SelectionSet::copy_ids).
    pub fn ids(&self) -> Vec<u64> {
        let n = self.len();
        let mut out = vec![0u64; n];
        let written = self.copy_ids(&mut out);
        out.truncate(written as usize);
        out
    }
}

impl Default for SelectionSet {
    fn default() -> Self {
        Self::new()
    }
}
