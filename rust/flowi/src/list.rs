//! Virtualized lists ([`List`]) and the caller-owned [`SelectionSet`] they select into, plus
//! the value types they exchange ([`ListItem`], [`ListOpts`], [`Reorder`], [`SelectionRequest`]).

use std::os::raw::c_char;
use std::ptr;

use crate::sys;

use crate::frame_thread::frame_thread_guard;
use crate::{ensure_fl_init, DropRelationship, ImageHandle, LayoutId, List, ListRange};

/// Scrollable, virtualized (uniform-row-height) lists of caller-emitted rows. This is the scoped face,
/// for rows with custom content. Open the list with [`List::begin`] (returns the visible [`ListRange`] to emit), then for each index
/// in start..end bracket the row's content between [`List::item_begin`] (passing the row's opaque u64 stable
/// id; returns its interaction [`Signal`](crate::Signal)) and [`List::item_end`], and close
/// with [`List::end`] (returns the [`ListResult`]). The List never interprets the stable id - it is
/// pure identity.
/// [`List::begin`] takes any [`LayoutId`] like [`Layout::start`](crate::Layout::start).
impl List {
    /// Begin a virtualized list identified by id over the app's full ordered id slice ordered_ids,
    /// applying config. Call inside a frame callback. Returns the visible [`ListRange`] to emit
    /// (start..end); pair with [`List::end`]. The id is any [`LayoutId`] (string, or (name, seed) tuple
    /// to disambiguate repeated lists in a loop).
    ///
    /// The List runs the selection gestures internally (plain click replaces, ctrl/⌘-click toggles,
    /// shift-click ranges, Ctrl+A selects all) and, when set is Some, auto-applies the resulting
    /// [`SelectionRequest`]s into it (resolving ranges against ordered_ids) and draws the row highlight from
    /// set membership. Pass None to opt out: the List applies and highlights nothing, and the caller drives
    /// selection from [`List::requests`]. The emitted requests are always available on the [`ListResult`]
    /// regardless of set.
    ///
    /// # Panics
    /// Panics if ordered_ids holds more than `u32::MAX` ids, or when called off the thread that owns the
    /// live [`Application`](crate::Application).
    pub fn begin(
        id: impl Into<LayoutId>,
        ordered_ids: &[u64],
        set: Option<&mut SelectionSet>,
        config: ListConfig<'_>,
    ) -> ListRange {
        let set_ptr = match set {
            Some(s) => s.handle,
            None => ptr::null_mut(),
        };
        let count = u32::try_from(ordered_ids.len()).expect("a List holds at most u32::MAX ids");
        let id: LayoutId = id.into();
        let _frame = frame_thread_guard("List::begin");
        let labels = if config.labels.len() == ordered_ids.len() {
            frame_labels(config.labels)
        } else {
            ptr::null()
        };
        // SAFETY: frame_ids copies ordered_ids into the frame arena, which outlives the List's begin..end
        // scope, so the pointer the List retains until List::end never reaches the caller's slice; set_ptr is
        // either null or the handle of the &mut SelectionSet borrowed here, which the List may mutate for the
        // duration of the call and no longer. config.to_sys carries only frame-arena pointers, and _frame
        // keeps the call to the frame context's thread.
        unsafe {
            sys::fl_list_begin(
                id.into_sys(),
                frame_ids(ordered_ids),
                count,
                set_ptr,
                config.to_sys(labels),
            )
        }
    }

    /// End the list and return the per-frame [`ListResult`], keyed on stable ids. Pair with [`List::begin`].
    ///
    /// # Panics
    /// Panics when called off the thread that owns the live [`Application`](crate::Application).
    pub fn end() -> ListResult {
        let _frame = frame_thread_guard("List::end");
        // SAFETY: a plain call that closes the List opened by begin; the result's pointers are read, and
        // copied out, by from_sys before any further List call.
        ListResult::from_sys(unsafe { sys::fl_list_end() })
    }

    /// The Selection Requests the List emitted this frame, read from a [`ListResult`]. Empty when the gesture
    /// made no change. Useful on the None-set path (the caller owns apply/highlight); on the auto-apply path
    /// they have already been folded into the set.
    pub fn requests(result: &ListResult) -> Vec<SelectionRequest> {
        result.requests.clone()
    }

    /// The reorder request emitted on the release frame of a drag-reorder, or None when no drop happened this
    /// frame. The app owns the model mutation: move every id in [`Reorder::from_ids`] to sit
    /// [`Reorder::rel`] the [`Reorder::target`] id. The relationship is always Before or After.
    pub fn reorder(result: &ListResult) -> Option<Reorder> {
        result.reorder_emitted.then(|| result.reorder())
    }

    /// The live state of an in-flight reorder drag, or None when no drag is active this frame. Same shape as the
    /// committed [`Reorder`], but the target/rel are the live CANDIDATE drop position. Exposed so a caller
    /// that set [`ListConfig::suppress_drag_visuals`] can draw its own ghost / insertion line from these fields.
    pub fn drag_state(result: &ListResult) -> Option<Reorder> {
        result.drag_active.then(|| result.reorder())
    }

    /// The declarative one-call List, for rows that are a label and an optional icon. Hand it the rows as [`ListItem`] descriptors ({ id, label, icon }) plus the
    /// caller-owned set (None opts out of auto-apply/highlight); the List runs the whole interaction
    /// internally - virtualization, the selection gesture machine, keyboard nav, marquee, drag-reorder, the
    /// highlight/ghost/insertion-line drawing, and auto-applying selection - and draws each row's label + icon.
    /// The caller reads only the policy outcomes off the [`ListResult`]: activated_id, context_menu_id
    /// (see [`List::context_menu`]), and the reorder request (see [`List::reorder`]). No per-row loop, no
    /// apply, no [`Signal`](crate::Signal) handling. opts carries the layout knobs plus the multi_select / reorderable
    /// policy toggles; the scoped [`List::begin`] face stays for rows with custom content. Call inside a frame.
    ///
    /// # Panics
    /// Panics when called off the thread that owns the live [`Application`](crate::Application).
    pub fn show(
        id: impl Into<LayoutId>,
        items: &[ListItem<'_>],
        set: Option<&mut SelectionSet>,
        opts: ListOpts,
    ) -> ListResult {
        let set_ptr = match set {
            Some(s) => s.handle,
            None => ptr::null_mut(),
        };
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
                sys_items.len() as u32,
                set_ptr,
                opts.to_sys(),
            )
        };
        ListResult::from_sys(raw)
    }

    /// The right-clicked row's stable id from a [`ListResult`], or None when no row was right-clicked this
    /// frame - the declarative face's context-menu hook. The List opens no menu itself; the app opens its own at
    /// the cursor for this id. Available on both faces (the scoped face populates it from a row's right-click too).
    pub fn context_menu(result: &ListResult) -> Option<u64> {
        (result.context_menu_id != 0).then_some(result.context_menu_id)
    }
}

/// Configuration for [`List::begin`]. [`Default`] is the C zero default: the List's own row height and gap,
/// no type-ahead labels, built-in drag visuals.
#[derive(Copy, Clone, Debug)]
pub struct ListConfig<'a> {
    /// Uniform row height in logical pixels (0 -> default 32).
    pub row_height: f32,
    /// Gap between rows in logical pixels (0 -> default 8).
    pub gap: f32,
    /// Per-row labels for keyboard type-ahead, parallel to the ordered id slice passed to [`List::begin`].
    /// Empty, or a length different from the id slice's, disables type-ahead. The strings are copied into
    /// the per-frame arena at the call.
    pub labels: &'a [&'a str],
    /// false (default): the List draws the built-in drag ghost + insertion line. true: it draws neither, so
    /// the caller draws its own from [`List::drag_state`].
    pub suppress_drag_visuals: bool,
}

impl Default for ListConfig<'_> {
    fn default() -> Self {
        ListConfig {
            row_height: 0.0,
            gap: 0.0,
            labels: &[],
            suppress_drag_visuals: false,
        }
    }
}

impl ListConfig<'_> {
    /// Lower to the `#[repr(C)]` mirror the C List consumes, with labels the frame-arena array already
    /// built from [`ListConfig::labels`] (null for none).
    fn to_sys(self, labels: *const sys::RawStr) -> sys::ListConfig {
        sys::ListConfig {
            row_height: self.row_height,
            gap: self.gap,
            labels,
            suppress_drag_visuals: self.suppress_drag_visuals,
        }
    }
}

/// The per-frame outcome of a [`List`], returned by [`List::end`] and [`List::show`]. It owns everything it
/// carries: the selection requests and dragged ids are copied out of the C List's frame storage when the
/// result is built, so no field points into memory the next frame reuses. It is not [`Copy`] and has no
/// public constructor. The stable ids are the app's own opaque u64s; 0 means "none".
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ListResult {
    activated_id: u64,
    focused_id: u64,
    context_menu_id: u64,
    requests: Vec<SelectionRequest>,
    nav_up_edge: bool,
    nav_down_edge: bool,
    drag_active: bool,
    reorder_emitted: bool,
    drag_from_ids: Vec<u64>,
    drag_target_id: u64,
    drag_rel: DropRelationship,
}

impl ListResult {
    /// Copy a C result and the frame storage it points at into an owned value.
    fn from_sys(raw: sys::ListResult) -> Self {
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
        ListResult {
            activated_id: raw.activated_id,
            focused_id: raw.focused_id,
            context_menu_id: raw.context_menu_id,
            requests,
            nav_up_edge: raw.nav_up_edge,
            nav_down_edge: raw.nav_down_edge,
            drag_active: raw.drag_active,
            reorder_emitted: raw.reorder_emitted,
            drag_from_ids,
            drag_target_id: raw.drag_target_id,
            drag_rel: raw.drag_rel,
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

    /// Stable id of the item activated this frame (double-click / Enter), or 0.
    pub fn activated_id(&self) -> u64 {
        self.activated_id
    }

    /// Stable id of the focused item, or 0.
    pub fn focused_id(&self) -> u64 {
        self.focused_id
    }

    /// Nav-Up pressed while the focus cursor was already on the first row.
    pub fn nav_up_edge(&self) -> bool {
        self.nav_up_edge
    }

    /// Nav-Down pressed while the focus cursor was already on the last row.
    pub fn nav_down_edge(&self) -> bool {
        self.nav_down_edge
    }
}

/// Copy labels into the frame arena as an FlString array and return it; null when labels is empty or too
/// long to count in a u32. Must run inside a frame.
fn frame_labels(labels: &[&str]) -> *const sys::RawStr {
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

/// Copy ids into the frame arena and return the copy; an empty slice yields its own dangling pointer, which a
/// zero count never reads. Must run inside a frame.
fn frame_ids(ids: &[u64]) -> *const u64 {
    const _: () = assert!(
        std::mem::size_of::<sys::RawStr>() >= 2 * std::mem::size_of::<u64>()
            && std::mem::align_of::<sys::RawStr>() >= std::mem::align_of::<u64>()
    );
    if ids.is_empty() {
        return ids.as_ptr();
    }
    let slots = u32::try_from(ids.len().div_ceil(2)).expect("a List holds at most u32::MAX ids");
    // SAFETY: fl_frame_str_array returns slots * 16 bytes of frame-arena storage aligned for an FlString
    // (8 bytes), which holds ids.len() u64s; the copy fills the first ids.len() of them. The storage stays
    // valid until the frame ends. It asserts an active frame, which List::begin runs in.
    unsafe {
        let dst = sys::fl_frame_str_array(slots) as *mut u64;
        ptr::copy_nonoverlapping(ids.as_ptr(), dst, ids.len());
        dst
    }
}

/// One row descriptor for [`List::show`]. Carries the row's opaque u64 stable id (pure identity; the
/// selection and result key), the label the List renders and type-aheads on (borrowed for the [`List::show`]
/// call; copied into the per-frame arena), and an optional leading icon (None renders label-only).
#[derive(Copy, Clone, Debug)]
pub struct ListItem<'a> {
    /// Opaque stable id - never interpreted; the selection / activation / reorder key.
    pub id: u64,
    /// Row text the List draws and uses for keyboard type-ahead.
    pub label: &'a str,
    /// Optional leading icon; None (or a not-ready handle) draws nothing.
    pub icon: Option<ImageHandle>,
}

/// Options for [`List::show`]. multi_select (default true) gates ctrl/shift/marquee/Ctrl+A; reorderable (default true) gates
/// drag-reorder; suppress_drag_visuals lets the caller draw its own affordances. Row height / gap default to
/// the List's own (0 -> 32 / 8).
#[derive(Copy, Clone, Debug)]
pub struct ListOpts {
    /// Uniform row height in logical pixels (0 -> default 32).
    pub row_height: f32,
    /// Gap between rows in logical pixels (0 -> default 8).
    pub gap: f32,
    /// true (default): full multi-select. false: single-selection (plain replace only).
    pub multi_select: bool,
    /// true (default): a row press can begin a drag-reorder. false: rows never start a reorder drag.
    pub reorderable: bool,
    /// true: the caller draws its own ghost/insertion line from the result's `drag_*`; false (default): the
    /// List draws the built-in affordances.
    pub suppress_drag_visuals: bool,
}

impl Default for ListOpts {
    fn default() -> Self {
        ListOpts {
            row_height: 0.0,
            gap: 0.0,
            multi_select: true,
            reorderable: true,
            suppress_drag_visuals: false,
        }
    }
}

impl ListOpts {
    /// Lower to the `#[repr(C)]` mirror the C ui_list consumes.
    fn to_sys(self) -> sys::ListOpts {
        sys::ListOpts {
            row_height: self.row_height,
            gap: self.gap,
            multi_select: self.multi_select,
            reorderable: self.reorderable,
            suppress_drag_visuals: self.suppress_drag_visuals,
        }
    }
}

/// A drag-reorder request a [`List`] hands back. The List runs the
/// gesture and draws the affordances; the app owns the model mutation - move every id in from_ids so it sits
/// rel the target id. Dragging any row of a multi-selection drags the whole selection (from_ids lists all
/// of them); dragging an unselected row drags just it. Returned by [`List::reorder`] on the release frame; the
/// same shape is returned by [`List::drag_state`] every in-flight frame as the live candidate.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Reorder {
    /// The dragged stable ids (the whole selection if the pressed row was selected, else just that row).
    pub from_ids: Vec<u64>,
    /// The stable id the drop is positioned relative to.
    pub target: u64,
    /// Where the drop lands relative to target: always Before or After.
    pub rel: DropRelationship,
}

/// A single selection change a [`List`] emits per frame, applied to a [`SelectionSet`] with
/// [`SelectionSet::apply`]. Ranges name their two endpoint stable ids (never the span), so
/// they survive virtualization - [`SelectionSet::apply`] resolves them against the app's ordered id list.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum SelectionRequest {
    /// Empty the set.
    ClearAll,
    /// Select (true) or deselect (false) every item in the ordered model.
    SetAll(bool),
    /// Select/deselect a single item by stable id.
    SetOne(u64, bool),
    /// Select/deselect the inclusive span between two endpoint stable ids (from, to); order-agnostic.
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

    /// Lift a flat C mirror (as emitted by the List on its [`ListResult`]) into this ergonomic enum, dropping
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

/// The caller-owned storage of selected [`List`] stable ids, freed on drop. Membership is read with
/// [`contains`](SelectionSet::contains) and edited with [`set`](SelectionSet::set) /
/// [`set_all`](SelectionSet::set_all) / [`clear`](SelectionSet::clear) or by folding in a batch of
/// [`SelectionRequest`]s with [`apply`](SelectionSet::apply).
pub use crate::generated::ui_selection::SelectionSet;

impl SelectionSet {
    /// Create an empty selection set with a default initial capacity. Usable without a live [`Application`](crate::Application) (the
    /// handle's arena is independent); only emitting a List needs a frame.
    pub fn new() -> Self {
        Self::with_capacity(0)
    }

    /// Create an empty selection set hinting capacity ids (a built-in floor applies; it also grows on demand).
    ///
    /// # Panics
    /// Panics if the native set cannot be allocated.
    pub fn with_capacity(capacity: u32) -> Self {
        ensure_fl_init();
        // SAFETY: a plain call over a u32; the base subsystems it allocates from are up (just ensured).
        let handle = unsafe { sys::fl_selection_set_create(capacity) };
        assert!(!handle.is_null(), "selection_set_create failed");
        SelectionSet { handle }
    }

    /// Fold a batch of requests into the set, in order. ordered_ids is the app's full ordered model, used to
    /// resolve any [`SelectionRequest::SetRange`] / [`SelectionRequest::SetAll`] span; pass an empty slice when no
    /// such request is present. The bytes do not escape the call.
    pub fn apply(&mut self, requests: &[SelectionRequest], ordered_ids: &[u64]) {
        let lowered: Vec<sys::SelectionRequest> = requests.iter().map(|r| r.to_sys()).collect();
        // SAFETY: self.handle is the live handle this set owns, and both slices are passed as
        // pointer/length pairs over storage that outlives the synchronous call - nothing escapes it.
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

    /// Number of selected ids.
    #[inline]
    pub fn len(&self) -> usize {
        // SAFETY: self.handle is the live handle this set owns.
        unsafe { sys::fl_selection_set_len(self.handle) as usize }
    }

    /// True if nothing is selected.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Snapshot the selected ids into an owned Vec (order unspecified) - the allocating convenience over
    /// [`copy_ids`](SelectionSet::copy_ids), which fills a caller's `&mut [u64]`.
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
