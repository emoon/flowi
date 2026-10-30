//! flowi: an immediate-mode UI toolkit.
//!
//! Create an [`Application`], then describe the UI once per frame: lay it out with the [`col!`] /
//! [`row!`] macros, emit widgets such as [`Button::new`], and react to the [`Signal`] each widget
//! returns.
//!
//! ```ignore
//! use flowi::{col, grow, row, Application, Button, Text};
//!
//! fn main() {
//!     let app = Application::new(800, 600, false);
//!     app.run(0u32, |clicks: &mut u32| {
//!         col! { width: grow!(), height: grow!() {
//!             row! { {
//!                 if Button::new("Click me", Default::default()).clicked {
//!                     *clicks += 1;
//!                 }
//!                 Text::emit(format!("clicked {clicks} time(s)"));
//!             } }
//!         }}
//!     });
//! }
//! ```
//!
//! [`Application::run`] blocks and calls the closure once per frame until the window closes;
//! [`Application::tick`] drives a single frame instead, for a caller that keeps its own loop. A
//! panic inside the frame closure aborts the process.
//!
//! An application that owns its own display and input - a kiosk shell, a plugin inside a host's
//! window, a frontend rendering to DRM - creates the same type with [`Application::embedded`] and
//! drives each frame itself with [`Application::tick_embedded`], [`Application::pixels`] and
//! [`Application::push_input`].
//!
//! A caller that must touch the process-wide foundation before there is an application uses the
//! free [`init`] / [`shutdown`] pair.

mod foundation;
mod sys;
mod vfs;

#[cfg(feature = "allocation-probe")]
pub use crate::sys::allocation_probe;
pub use crate::sys::allocator::HeapAllocator;
use std::os::raw::c_char;

/// The toolkit's value types: a colour, a polygon point, a 2D vector.
pub use crate::sys::{Color, PolyPoint, Vec2};

/// The one shared error enum - the Err arm of the `Result<T, FlError>` a fallible slot
/// returns.
pub use crate::sys::FlError;

/// Log severity, re-exported for direct use with the log control surface. Rust records
/// carry a log::Level instead; the bridge maps it onto this enum.
pub use crate::sys::LogLevel;

/// The opaque, Copy font handle returned by [`Font::load`] and consumed by [`Font::set`],
/// [`Font::is_loaded`], and [`Painter::text`] (sentinel 0 = invalid / "active font"). Callers treat it as a
/// token, never an integer.
pub use crate::sys::FontId;

/// The single-line text-measurement value types: [`TextStyle`] (font, fractional px size, letter spacing) is
/// what [`Text::measure`] and the painter styled draw take; [`TextMetrics`] (width + line metrics) is what
/// [`Text::measure`] returns; [`FontMetrics`] is the string-independent vertical subset [`Font::metrics`]
/// returns, for stable vertical centering of dynamic readouts.
pub use crate::sys::{FontMetrics, TextMetrics, TextStyle};

/// The dash pattern for a stroked outline (dash length, gap length, both logical px; zeroed = solid):
/// what [`LayoutBorder::stroke`] and [`Painter::circle_stroked`] take.
pub use crate::sys::StrokeStyle;

/// The fill styling one [`Painter::rect_styled`] draw takes: per-corner radius ([`CornerRadius`]) plus
/// per-corner colors, which is the only way to express a rounded gradient rect - [`Painter::rect_rounded`]
/// is solid-only and [`Painter::gradient_rect`] is square-only, and the per-element scissor is a plain rect,
/// so a rounded well cannot be faked by clipping.
pub use crate::sys::RectConfig;

/// The opaque, Copy image handle returned by [`Image::load`] and consumed by [`Image::is_ready`],
/// and [`Painter::image`] (sentinel 0 = invalid).
pub use crate::sys::ImageHandle;

/// The resolved within-frame identity of a laid-out element - a one-way hash of its
/// [`LayoutId`]. Returned by [`hot_element`] (the topmost hovered element)
/// and produced by [`LayoutId::hashed_id`]; the two share this type so a returned hot id compares directly
/// against one you hashed yourself. The hash is one-way, so map a hot id back to your own object by hashing your
/// own candidate ids into a `{ElementId -> object}` map - not by reversing the id. It is a per-frame hash, not
/// a stable cross-frame handle (the stable identity is the [`LayoutId`]).
pub use crate::sys::ElementId;

/// The LayoutConfig tree, re-exported for callers building a container config to hand to
/// [`Layout::configure`]. The ergonomic layout macros build them fluently - see [`layout!`] / [`row!`] /
/// [`col!`] - but they remain public so callers can also construct the structs directly.
pub use crate::sys::{
    BorderWidthConfig, CornerRadius, LayoutAlignmentX, LayoutAlignmentY, LayoutBorder,
    LayoutChildAlignment, LayoutClip, LayoutConfig, LayoutDirection, LayoutPadding, LayoutPlaced,
    LayoutSizing, LayoutSizingAxis, LayoutSizingAxisSize, LayoutSizingMinMax, LayoutSizingType,
};

/// The design-token scales, re-exported for callers sizing and spacing their own widgets. [`Spacing`] (gaps,
/// padding), [`Radius`] (corner rounding), [`WidgetHeight`] (standard control heights) and [`BorderWidth`]
/// (stroke weights) are the same steps flowi's own widgets are built on, so an app-side widget placed beside a
/// built-in one lines up with it instead of drifting by a pixel or two.
pub use crate::sys::{BorderWidth, Radius, Spacing, WidgetHeight};

/// The FloatingConfig / ScrollAreaConfig trees, re-exported for callers building a floating or
/// scroll-area config to hand to [`Floating::configure`] / [`Scroll::area_begin`]. The [`floating!`] / [`scroll!`]
/// macros build these fluently, but they stay public for direct construction.
pub use crate::sys::{
    FloatingAttachPoint, FloatingAttachTo, FloatingConfig, FloatingPointerCapture, ScrollAreaConfig,
};

/// The visible index window [`List::begin`] returns. The other List value types - [`ListConfig`], which
/// [`List::begin`] takes, and the per-frame [`ListResult`] [`List::end`] returns - are this crate's own.
pub use crate::sys::ListRange;

/// Where a [`List`] drag-reorder lands, relative to a target stable id. A [`List`] yields
/// Before or After.
pub use crate::sys::DropRelationship;

/// The layout DSL. Function-like macros that expand to the
/// begin/configure/body/end container calls, hiding the config struct: [`layout!`] (general, explicit
/// direction), [`row!`] / [`col!`] (direction presets), [`scroll!`] (a scroll area), and [`floating!`] (a
/// floating element). These macros are the primary layout API; the raw [`Layout::start`] /
/// [`Layout::configure`] / [`Layout::end`] trio covers what the macros do not take (e.g. an
/// Element-anchored floating element, which needs a hashed parent_id).
///
/// Sizing for the width / height fields is given by the sub-macros [`fixed!`], [`grow!`], [`percent!`], and
/// [`fit!`]; the remaining LayoutConfig fields have their own sub-macros too - [`clip!`] (clipping +
/// manual scroll), [`border!`] (color + uniform width), and [`align!`] (child alignment). Each container macro takes an optional positional string id
/// (a stable file:line:column id is synthesised when omitted), flat name: value fields, and a trailing { } body
/// block.
///
/// padding: and radius: both take a scalar or a per-side/per-corner value, via Into: padding: accepts a
/// u16 (uniform), a (vertical, horizontal) pair, a `[top, right, bottom, left]` array - CSS's shorthand
/// orders - or a [`LayoutPadding`]; radius: accepts an f32 (uniform) or a [`CornerRadius`]. An unsuffixed
/// integer literal still infers for padding:, because u16 is the only integer form.
///
/// [`fill`] is the flexible spacer that goes between containers - CSS's margin-left: auto.
pub use flowi_macros::{
    align, border, clip, col, fit, fixed, floating, grow, layout, percent, placed, row, scroll,
};

/// A flexible spacer: an empty container that grows on both axes, eating the slack its siblings leave.
/// This is how you spell CSS's margin-left: auto - put one before a child to push it to the end of the
/// container, or between two children to push them apart.
///
/// The cross-axis grow is invisible where it matters: a grown child contributes no minimum to a [`fit!`]-sized
/// parent, so a fill inside a fit!-height row leaves that row's height alone.
///
/// It carries no id of its own, so fill is safe inside a loop and inside a helper called any number of
/// times.
pub fn fill() {
    Layout::start_auto();
    Layout::configure(LayoutConfig {
        sizing: LayoutSizing {
            width: LayoutSizingAxis::GROW,
            height: LayoutSizingAxis::GROW,
        },
        ..Default::default()
    });
    Layout::end();
}

/// The context-menu macros, re-exported for callers. [`menu!`] drives the Menu::begin/body/Menu::end
/// pair with its body running only while the menu is open; [`menu_item!`] emits one action item and returns
/// its [`Signal`]; [`menu_check!`] / [`menu_radio!`] emit the stateful item kinds over a &mut bound value;
/// [`menu_separator!`] / [`menu_header!`] emit the static (non-interactive) rows; [`submenu!`] drives the
/// UiSubmenu::begin/body/UiSubmenu::end pair with its body running only while the submenu is
/// expanded; [`menu_region!`] wraps a right-click target that opens the menu at the cursor.
pub use flowi_macros::{
    menu, menu_check, menu_header, menu_item, menu_radio, menu_region, menu_separator, submenu,
};

/// The menu-bar macros, re-exported for callers. [`menubar!`] drives the
/// MenuBar::begin/body/MenuBar::end pair with its body - emitting the tops - running every frame (the strip
/// is always visible); [`menubar_menu!`] drives the UiMenubarMenu::begin/body/UiMenubarMenu::end pair for one
/// top with its body running only while that top's dropdown is open. Dropdown items are the same
/// [`menu_item!`] action items as a context menu.
pub use flowi_macros::{menubar, menubar_menu};

/// The rich widget interaction result, re-exported for callers reading a widget's response. Returned by
/// value from [`Button::new`]; read [`Signal::clicked`](Signal#structfield.clicked) and the other
/// bool channels (hovered, focused, pressed, ...) directly.
pub use crate::sys::Signal;

/// The per-element behavior opt-in mask, re-exported for callers driving the generic [`Item::behavior`]
/// primitive: compose the behaviors a custom element reacts to with ItemFlags::CLICKABLE |
/// ItemFlags::DRAGGABLE | ItemFlags::DOUBLE_CLICK | ItemFlags::RIGHT_CLICK, then read the matching channels
/// off the returned [`Signal`].
pub use crate::sys::ItemFlags;

/// The generated façade: one `pub struct <Domain>` of forwarding methods per API block.
mod generated;

/// The typed arena API, from the foundation crate: &self alloc / &mut self rewind,
/// closure-scoped temp/scratch, and the ArenaVec/ArenaString collections.
pub use crate::foundation::{Arena, ArenaString, ArenaVec, BaseArena};

/// Asynchronous VFS results: an owning `Handle<T>` polls to `Result<T, FlError>` and closes its
/// request without blocking on drop.
pub use crate::vfs::{Handle, HandleFuture, Payload, Poll};

/// A single-threaded, frame-ticked local executor for cooperative multi-step async chains:
/// spawn a 'static owning-output async fn, tick it once per frame, drop its [`Task`]
/// to cancel. Awaits [`Handle`] leaves via [`HandleFuture`].
pub use crate::vfs::{LocalExecutor, Task};

/// Reading files and listing directories through flowi's VFS: [`Vfs`] opens a
/// mount, [`Mount`] carries every operation, [`WorkerMount`] is the blocking view a
/// job body reads through.
pub use crate::vfs::{
    Entry, FileList, MappedRead, Mount, MountError, MountInfo, MountOptions, MountStatus,
    OwnedMount, Vfs, VfsData, WorkerMount,
};

/// What an embedding host hands flowi and gets back - decoded OS events in, rendered
/// pixels out.
mod host;

/// The embedded-mode vocabulary: the events an embedding host pushes in, and the pixels
/// it presents itself. The tier they drive is [`Application::embedded`].
pub use host::{InputEvent, Pixels};

/// The decoded-event vocabulary [`Application::push_input`] takes; every payload type sits
/// inside [`InputEvent`].
pub use crate::sys::{GamepadEvent, KeyEvent, MouseEvent, TextInputEvent, WindowEvent};

/// The insides of those events: the type tag that selects a payload union's live arm, the
/// unions themselves, and the per-arm data structs - what a host decoding OS events
/// constructs.
pub use crate::sys::{
    GamepadAxis, GamepadAxisData, GamepadButtonData, GamepadEventPayload, GamepadEventType,
    KeyEventType, MouseButtonData, MouseEventPayload, MouseEventType, MouseMoveData,
    MouseRelativeData, MouseWheelData, WindowEventPayload, WindowEventType, WindowResizeData,
};

/// The keyboard modifier bitmask carried on a [`KeyEvent`] - the typed view of its modifiers
/// field, and what [`UiInput::keyboard_modifiers`] returns.
pub use crate::sys::KeyModifier;

/// The pad button [`UiInput::gamepad_button_down`] polls, carried on a [`GamepadEvent`]'s button arm.
/// GamepadButton::Count is the array-size sentinel the C enum carries, not a button.
pub use crate::sys::GamepadButton;

/// Watching a directory for file changes, for a host that hot-reloads content.
pub use crate::foundation::{FileChange, FileChanges, FileWatcher, WatchConfig};
/// The change-kind bitmask [`FileChange::change_types`] reports.
pub use crate::sys::FileChangeType;

/// Rust-side logging: the log-crate bridge onto flowi's log channels and the RAII on-disk sink. Rust code logs with log::info! / log::warn!(target: "AUDIO",
/// ...); targets name channels 1:1 and everything untargeted lands on "CORE", so C and Rust
/// records interleave in one sink.
///
/// [`install_log_bridge`] routes this binary's records at those channels - automatic for a
/// direct-link host, installed by hand by a dlopen'd Rust cdylib, which has its own log
/// global. [`set_target_level`] drops a noisy dependency's records below a level;
/// [`LogFile`] is the on-disk sink, open for as long as the guard lives.
pub use crate::foundation::{install_log_bridge, set_target_level, LogFile};

/// flowi's global work-stealing job system: schedule a Rust closure with [`Jobs::add`], then
/// query or wait on the returned [`JobHandle`].
pub use crate::foundation::{JobHandle, JobPriority, Jobs, JobsResult, WorkerInfo};

/// Profiler scopes as RAII guards.
mod perf;

/// The scope guards; the [`perf_scope!`] macro is exported at the crate root beside them.
pub use perf::{PerfFrame, PerfScope};

/// Which cores a heterogeneous SoC offers, and how to pin a thread to a chosen set of them.
mod cpu;

/// The topology query and the affinity call.
pub use cpu::{performance_cores, pin_current_thread};

/// Arguments for a localized message that carries runtime values, and the [`loc_fmt!`] /
/// [`loc_plural!`] sugar that builds them at the call site.
mod localization;

/// The format-argument type and the lookup calls the macros expand to; [`Localization`] carries the
/// language switch itself.
pub use localization::{FmtArg, Localize};

/// Point flowi at a language manifest and switch languages at runtime.
pub use generated::localization::Localization;

/// Content digests, identical to the ones flowi computes internally over the same bytes.
pub mod hash;

/// Path manipulation under core's rules - for a path that came out of a manifest, an archive or a
/// mount, where the answer has to match flowi's own path handling.
pub use crate::foundation::path;

/// Conveniences for the raw layout escape hatch - drop-guarded [`Layout`] container closures
/// and one-field [`TextConfig`] / [`WidgetConfig`] / [`DropdownConfig`] builders for the call
/// sites that build a [`LayoutConfig`] by hand instead of through the layout macros.
pub mod layout_ext;

/// The digest surfaces most callers reach for; the rest of the algorithms live in [`hash`].
pub use hash::{sha256_hex, Sha256};

/// The 64-bit FNV-1a of text, identical to the hash flowi computes internally.
#[inline]
pub fn fl_string_hash_fnv1a(text: &str) -> u64 {
    // SAFETY: the borrow outlives the call, which is all a RawStr view has to be valid for,
    // and the hash only reads the bytes.
    unsafe { crate::sys::fl_string_hash_fnv1a(crate::sys::RawStr::borrow(text)) }
}

/// Order two byte strings in flowi's natural order: a run of digits compares by numeric value, so Disk2
/// sorts before Disk10; everything else compares byte-wise, case-insensitively over ASCII. On a shared prefix
/// the shorter string sorts first.
///
/// This is the order flowi sorts names in internally. Two corners: a longer digit run wins outright once leading zeros are dropped (10 after 9),
/// and bytes ≥ 0x80 compare as negative (as signed char).
#[inline]
pub fn natural_cmp(a: &str, b: &str) -> std::cmp::Ordering {
    natural_cmp_bytes(a.as_bytes(), b.as_bytes())
}

/// [`natural_cmp`] over raw bytes, for names that carry no UTF-8 guarantee - a database's string pool, a
/// record read straight off disk. The comparison is byte-wise either way, so this is the same ordering, not a
/// looser one.
#[inline]
pub fn natural_cmp_bytes(a: &[u8], b: &[u8]) -> std::cmp::Ordering {
    // SAFETY: both borrows outlive the call, which is all a RawStr view has to be valid for, and the
    // comparator only reads the bytes.
    let order = unsafe {
        crate::sys::fl_string_compare_natural(
            crate::sys::RawStr::borrow_bytes(a),
            crate::sys::RawStr::borrow_bytes(b),
        )
    };
    order.cmp(&0)
}

/// The end-cap flags [`Painter::arc`] takes.
pub use crate::sys::ArcFlags;
/// The workspace edge [`Dock::dock_outer`] attaches a new region to.
pub use crate::sys::DockSide;
/// The fill/close flags [`Painter::polyline`] takes.
pub use crate::sys::PolylineFlags;
pub use generated::background::Background;
pub use generated::button::Button;
pub use generated::checkbox::Checkbox;
pub use generated::chip::Chip;
pub use generated::cursor::Cursor;
pub use generated::dock::Dock;
pub use generated::dropdown::Dropdown;
pub use generated::focused::Focused;
pub use generated::font::Font;
/// The frame envelope: full-screen UI opacity, plus this frame's delta time and screen size - the two reads an
/// animation integrator and a full-screen layout need.
pub use generated::frame::Frame;
pub use generated::icon::Icon;
pub use generated::image::Image;
pub use generated::item_behavior::Item;
pub use generated::link::Link;
pub use generated::modal::Modal;
pub use generated::painter::Painter;
pub use generated::pill::Pill;
pub use generated::popup::{Popup, PopupScope};
pub use generated::progress_bar::ProgressBar;
pub use generated::radio_group::RadioGroup;
pub use generated::scrollbar::Scrollbar;
pub use generated::spacer::Spacer;
pub use generated::switch::Switch;
pub use generated::tab_bar::TabBar;
pub use generated::text::Text;
pub use generated::theme::Theme;
pub use generated::ui_floating::Floating;
pub use generated::ui_input::Input as UiInput;
pub use generated::ui_layout::Layout;
pub use generated::ui_list::List;
pub use generated::ui_menu::{
    Menu, Menubar as MenuBar, MenubarMenu as UiMenubarMenu, Submenu as UiSubmenu,
};
pub use generated::ui_scroll::Scroll;
pub use generated::viewport3d::{Mesh, Viewport3d};

/// The 3D seam's value types: the vertex the one fixed mesh format takes, the handle [`Mesh::create`]
/// returns, and the sampling filter [`Viewport3d::draw_mesh`] picks.
pub use crate::sys::{ImageFilter, MeshHandle, MeshVertex};

/// The virtualized-list helper: given a total item count, a uniform row height and the current scroll offset,
/// [`ListClipper::create`] returns the visible start_index..end_index window, so a long list only emits the
/// rows actually on screen.
pub use generated::list_clipper::ListClipper;

/// Per-call options for [`Image::new`].
pub use crate::sys::ImageOpts;

/// The shared per-widget config every widget slot takes - id override, explicit colours, variant/size token,
/// focus and tab order, per-widget theme override. id_override is a plain &str copied into the per-frame
/// arena at the call, and theme_override an `Option<&ThemeOverride>`. Build one with
/// ..Default::default(); [`Default`] is the zero default (normal id source, themed colours, solid
/// variant, widget-default size).
///
/// ```no_run
/// # use flowi::{Button, WidgetConfig};
/// // Two buttons emitted from one source line need distinct ids; override them explicitly.
/// let sig = Button::new("Save", WidgetConfig { id_override: "save-toolbar", ..Default::default() });
/// ```
pub use generated::widget_config::WidgetConfig;

/// The three token enums [`WidgetConfig`] carries: [`WidgetVariant`] picks the chrome treatment (solid /
/// outline / ghost / soft), [`WidgetSize`] the control-height token, and [`Focusable`] whether the widget
/// joins the keyboard focus order. Re-exported so setting any of them needs nothing from flowi_sys; all
/// three default to their 0 variant, which is what ..Default::default() leaves them at - for
/// [`Focusable`] that is Default, keeping the widget family's normal focus behaviour rather than turning
/// focus off.
pub use crate::sys::{Focusable, WidgetSize, WidgetVariant};

/// The per-widget config structs the remaining widget slots take. Each carries its own string fields as
/// &str ([`DropdownConfig::cancel_label`], [`TextInputConfig::placeholder`] / allowed_chars) and embeds
/// [`WidgetConfig`] as its widget field, so an id override or a per-widget theme override is reachable
/// through any of them. Each is zeroed under ..Default::default().
///
/// ```no_run
/// # use flowi::{DropdownConfig, WidgetConfig};
/// let config = DropdownConfig {
///     cancel_label: "Dismiss",
///     widget: WidgetConfig { id_override: "track-preset", ..Default::default() },
///     ..Default::default()
/// };
/// ```
pub use generated::widget_config::{
    CheckboxConfig, DropdownConfig, LinkConfig, ProgressConfig, RadioConfig, ScrollbarConfig,
    SwitchConfig, TabBarConfig, TextInputConfig,
};

/// The orientation token [`ScrollbarConfig`] carries; it defaults to
/// [`ScrollbarOrientation::Vertical`], the 0 variant.
pub use crate::sys::ScrollbarOrientation;

/// The raw C-layout types whose crate-root name belongs to an ergonomic front-end.
///
/// Some façade methods take these directly - Text::styled takes [`abi::TextConfig`], Icon::get takes
/// [`abi::Icon`], Cursor::set takes [`abi::Cursor`] - while the crate root's [`TextConfig`], [`Icon`] and
/// [`Cursor`] are the ergonomic forms. Prefer the root's forms. Fields here are ABI-shaped: raw strings
/// instead of &str, raw pointers instead of `Option<&T>`.
pub mod abi {
    /// Which built-in toolkit icon - the argument [`Icon::get`](crate::Icon::get) takes. The root's
    /// [`Icon`](crate::Icon) is the registry façade (load / get), so the icon selector lives here.
    pub use crate::sys::Icon;

    /// The cursor value [`Cursor::set`](crate::Cursor::set) takes and
    /// [`Cursor::get_requested`](crate::Cursor::get_requested) returns; build one with
    /// [`Cursor::system`](crate::Cursor::system). The root's [`Cursor`](crate::Cursor) is the request façade.
    pub use crate::sys::Cursor;

    /// The visible start_index..end_index window [`ListClipper::create`](crate::ListClipper::create)
    /// returns. The root's [`ListClipper`](crate::ListClipper) is the helper façade.
    pub use crate::sys::ListClipper;

    /// The raw element state [`UiInput::get_element_state`](crate::UiInput::get_element_state) returns, in
    /// physical px. The root's [`ElementState`](crate::ElementState) is the normalised form
    /// [`element_state`](crate::element_state) hands back, in logical (authoring) px - prefer it.
    pub use crate::sys::ElementState;

    /// The ABI config [`Text::styled`](crate::Text::styled) takes. The root's
    /// [`TextConfig`](crate::TextConfig) is the &str/`Option<FontId>` ergonomic twin.
    pub use crate::sys::TextConfig;

    /// The ABI twin of the safe [`WidgetConfig`](crate::WidgetConfig) - the widget field every ABI widget
    /// config embeds, for the ABI structs a shim crate hand-builds.
    pub use crate::sys::WidgetConfig;
}

/// The raw ABI string type: a non-owning counted view of bytes, the shape every string takes crossing
/// flowi's C ABI.
///
/// Every safe signature in this crate takes &str; RawStr is for code writing `extern "C"` entry points.
/// It promises nothing about how long its bytes live - that depends on which constructor made it.
pub use crate::sys::RawStr;

/// Raw C ABI items for ABI-shim and plugin-SDK crates that hand-write extern "C" entry points: the panic
/// guard, the opaque `FlArena`, the raw arena / jobs / perf-scope calls and the arena marshalling helpers.
/// Prefer the safe wrappers ([`Arena`], [`Jobs`], [`FileWatcher`]) in application code.
pub use crate::foundation::{
    arena_alloc_array, arena_box, borrow_slice, borrow_slice_mut, clear_poison, ffi_guard,
    fl_arena_create, fl_arena_destroy, fl_jobs_add_job, fl_jobs_add_job_with_dependency,
    fl_jobs_is_finished, fl_jobs_wait, fl_perf_scope_begin, fl_perf_scope_end,
    fl_perf_scope_frame_begin, fl_perf_scope_frame_end, set_panic_logger, FlArena, JobsWorkerInfo,
};

/// Raw C ABI item; prefer [`Mount`]. The opaque C FlVfsMount behind `*mut FlVfsMount`.
pub use crate::sys::FlVfsMount;

/// Raw C ABI item. The display rectangle a backend is asked to present into - the argument
/// backend_set_display_rect takes.
pub use crate::sys::DisplayRect;

/// Compile-time check that each flowi path names the same type as its flowi_sys counterpart.
mod public_surface_guard {
    macro_rules! nameable_from_flowi {
        ($($flowi_path:ty = $sys_type:ty),+ $(,)?) => {
            // The identity closure coerces to this fn pointer only when both paths name the same type.
            $(const _: fn($sys_type) -> $flowi_path = |v| v;)+
        };
    }

    nameable_from_flowi! {
        // Re-exported at the crate root under their own names.
        crate::Action = crate::sys::Action,
        crate::BackgroundMode = crate::sys::BackgroundMode,
        crate::BorderWidth = crate::sys::BorderWidth,
        crate::BorderWidthConfig = crate::sys::BorderWidthConfig,
        crate::Color = crate::sys::Color,
        crate::CornerRadius = crate::sys::CornerRadius,
        crate::ArcFlags = crate::sys::ArcFlags,
        crate::CursorShape = crate::sys::CursorShape,
        crate::DockSide = crate::sys::DockSide,
        crate::PolylineFlags = crate::sys::PolylineFlags,
        crate::DialogResult = crate::sys::DialogResult,
        crate::DropRelationship = crate::sys::DropRelationship,
        crate::ElementId = crate::sys::ElementId,
        crate::FileChangeType = crate::sys::FileChangeType,
        crate::FlError = crate::sys::FlError,
        crate::FloatingAttachPoint = crate::sys::FloatingAttachPoint,
        crate::FloatingAttachTo = crate::sys::FloatingAttachTo,
        crate::FloatingConfig = crate::sys::FloatingConfig,
        crate::FloatingPointerCapture = crate::sys::FloatingPointerCapture,
        crate::FontId = crate::sys::FontId,
        crate::FontMetrics = crate::sys::FontMetrics,
        crate::FontStyle = crate::sys::FontStyle,
        crate::GamepadAxis = crate::sys::GamepadAxis,
        crate::GamepadAxisData = crate::sys::GamepadAxisData,
        crate::GamepadButton = crate::sys::GamepadButton,
        crate::GamepadButtonData = crate::sys::GamepadButtonData,
        crate::GamepadEvent = crate::sys::GamepadEvent,
        crate::GamepadEventPayload = crate::sys::GamepadEventPayload,
        crate::GamepadEventType = crate::sys::GamepadEventType,
        crate::ImageFormat = crate::sys::ImageFormat,
        crate::ImageHandle = crate::sys::ImageHandle,
        crate::ImageLoadOptions = crate::sys::ImageLoadOptions,
        crate::ImageLoadPriority = crate::sys::ImageLoadPriority,
        crate::ImageOpts = crate::sys::ImageOpts,
        crate::ImageStatus = crate::sys::ImageStatus,
        crate::ItemFlags = crate::sys::ItemFlags,
        crate::Key = crate::sys::Key,
        crate::KeyEvent = crate::sys::KeyEvent,
        crate::KeyEventType = crate::sys::KeyEventType,
        crate::KeyModifier = crate::sys::KeyModifier,
        crate::LayoutAlignmentX = crate::sys::LayoutAlignmentX,
        crate::LayoutAlignmentY = crate::sys::LayoutAlignmentY,
        crate::LayoutBorder = crate::sys::LayoutBorder,
        crate::LayoutChildAlignment = crate::sys::LayoutChildAlignment,
        crate::LayoutClip = crate::sys::LayoutClip,
        crate::LayoutConfig = crate::sys::LayoutConfig,
        crate::LayoutDirection = crate::sys::LayoutDirection,
        crate::LayoutPadding = crate::sys::LayoutPadding,
        crate::LayoutPlaced = crate::sys::LayoutPlaced,
        crate::LayoutSizing = crate::sys::LayoutSizing,
        crate::LayoutSizingAxis = crate::sys::LayoutSizingAxis,
        crate::LayoutSizingAxisSize = crate::sys::LayoutSizingAxisSize,
        crate::LayoutSizingMinMax = crate::sys::LayoutSizingMinMax,
        crate::LayoutSizingType = crate::sys::LayoutSizingType,
        crate::ListRange = crate::sys::ListRange,
        crate::LogLevel = crate::sys::LogLevel,
        crate::MenuClosePolicy = crate::sys::MenuClosePolicy,
        crate::MouseButton = crate::sys::MouseButton,
        crate::MouseButtonData = crate::sys::MouseButtonData,
        crate::MouseEvent = crate::sys::MouseEvent,
        crate::MouseEventPayload = crate::sys::MouseEventPayload,
        crate::MouseEventType = crate::sys::MouseEventType,
        crate::MouseMoveData = crate::sys::MouseMoveData,
        crate::MouseRelativeData = crate::sys::MouseRelativeData,
        crate::MouseWheelData = crate::sys::MouseWheelData,
        crate::NavEdge = crate::sys::NavEdge,
        crate::NavFrame = crate::sys::NavFrame,
        crate::Overflow = crate::sys::Overflow,
        crate::PolyPoint = crate::sys::PolyPoint,
        crate::PopupAnchor = crate::sys::PopupAnchor,
        crate::Radius = crate::sys::Radius,
        crate::RectConfig = crate::sys::RectConfig,
        crate::ResizeFilter = crate::sys::ResizeFilter,
        crate::ScrollAreaConfig = crate::sys::ScrollAreaConfig,
        crate::ScrollbarDisplayMode = crate::sys::ScrollbarDisplayMode,
        crate::ScrollbarOrientation = crate::sys::ScrollbarOrientation,
        crate::Signal = crate::sys::Signal,
        crate::Spacing = crate::sys::Spacing,
        crate::StrokeStyle = crate::sys::StrokeStyle,
        crate::TextInputEvent = crate::sys::TextInputEvent,
        crate::TextMetrics = crate::sys::TextMetrics,
        crate::TextStyle = crate::sys::TextStyle,
        crate::TextVAlign = crate::sys::TextVAlign,
        crate::ThemeBorder = crate::sys::ThemeBorder,
        crate::ThemeColors = crate::sys::ThemeColors,
        crate::ThemeFeedback = crate::sys::ThemeFeedback,
        crate::ThemeInteractive = crate::sys::ThemeInteractive,
        crate::ThemeOverride = crate::sys::ThemeOverride,
        crate::ThemeSlot = crate::sys::ThemeSlot,
        crate::ThemeSurface = crate::sys::ThemeSurface,
        crate::ThemeText = crate::sys::ThemeText,
        crate::Vec2 = crate::sys::Vec2,
        crate::WidgetHeight = crate::sys::WidgetHeight,
        crate::WidgetSize = crate::sys::WidgetSize,
        crate::WidgetVariant = crate::sys::WidgetVariant,
        crate::WindowEvent = crate::sys::WindowEvent,
        crate::WindowEventPayload = crate::sys::WindowEventPayload,
        crate::WindowEventType = crate::sys::WindowEventType,
        crate::WindowResizeData = crate::sys::WindowResizeData,

        // Namespaced under abi.
        crate::abi::Cursor = crate::sys::Cursor,
        crate::abi::ElementState = crate::sys::ElementState,
        crate::abi::Icon = crate::sys::Icon,
        crate::abi::ListClipper = crate::sys::ListClipper,
        crate::abi::TextConfig = crate::sys::TextConfig,
        crate::abi::WidgetConfig = crate::sys::WidgetConfig,

        crate::RawStr = crate::sys::RawStr,
        crate::FlArena = crate::sys::Arena,
        crate::FlVfsMount = crate::sys::FlVfsMount,
        crate::DisplayRect = crate::sys::DisplayRect,
        crate::JobsWorkerInfo = crate::sys::JobsWorkerInfo,
    }
}

/// The image load/query vocabulary. [`ImageLoadOptions`] configures [`Image::load_mount`] (with [`BackgroundMode`],
/// [`ResizeFilter`] and [`ImageFormat`] selecting composition, sampling and decode format); [`ImageStatus`] is
/// what [`Image::status`] reports; [`ImageLoadPriority`] is what [`Image::set_load_priority`] takes.
pub use crate::sys::{
    BackgroundMode, ImageFormat, ImageLoadOptions, ImageLoadPriority, ImageStatus, ResizeFilter,
};

/// Mount-relative image loading, over the same [`Mount`] handle the VFS operations borrow.
impl Image {
    /// Start an asynchronous image load of relative_path within mount, applying options. Returns the
    /// image handle to poll with [`Image::status`], or 0 when the request could not be started. The load
    /// keeps reading through mount after this returns, so keep the mount open until the load completes.
    #[inline]
    pub fn load_mount(
        mount: &Mount,
        relative_path: impl AsRef<str>,
        options: ImageLoadOptions,
    ) -> crate::sys::ImageHandle {
        // SAFETY: the cast is between two opaque declarations of the same C FlVfsMount. mount is live
        // for the call, and the path is copied before the call returns.
        unsafe {
            crate::sys::fl_image_load_mount(
                mount.as_raw().cast(),
                crate::sys::RawStr::borrow(relative_path.as_ref()),
                options,
            )
        }
    }

    /// Upload pixels as a static image and return a handle that is ready immediately - the route for an
    /// image a program generates (a rendered waveform, a plotted curve, a procedural texture) and so has no
    /// file to [`Image::load`]. The buffer is copied during the call, so the caller may drop it on return.
    ///
    /// pixels must hold exactly width × height pixels tightly packed, at the byte width format implies:
    /// 1 for [`ImageFormat::Alpha`], 3 for [`ImageFormat::RGB`], 4 for [`ImageFormat::RGBA`] and
    /// [`ImageFormat::XRGB`]. There is no stride parameter - a buffer with padded rows must be repacked first.
    ///
    /// # Panics
    ///
    /// When pixels.len() is not that exact size, when width or height is negative, or when format is
    /// one the raw upload path does not accept ([`ImageFormat::Default`], [`ImageFormat::Deinterleaved`]).
    pub fn from_pixels(
        pixels: &[u8],
        width: i32,
        height: i32,
        format: ImageFormat,
    ) -> crate::sys::ImageHandle {
        let bytes_per_pixel = match format {
            ImageFormat::Alpha => 1usize,
            ImageFormat::RGB => 3,
            ImageFormat::RGBA | ImageFormat::XRGB => 4,
            ImageFormat::Default | ImageFormat::Deinterleaved => {
                panic!(
                    "Image::from_pixels does not accept {format:?}; use Alpha, RGB, RGBA or XRGB"
                )
            }
        };
        assert!(
            width >= 0 && height >= 0,
            "Image::from_pixels got a negative size ({width}x{height})"
        );
        let expected = (width as usize) * (height as usize) * bytes_per_pixel;
        assert!(
            pixels.len() == expected,
            "Image::from_pixels got {} bytes for a {width}x{height} {format:?} image, expected {expected}",
            pixels.len()
        );

        // SAFETY: the pointer addresses expected initialised bytes - exactly the extent the C side reads
        // for this size and format, as the assert above just established - and the image system copies them
        // out before returning.
        unsafe {
            crate::sys::fl_image_create_from_pixels(pixels.as_ptr().cast(), width, height, format)
        }
    }
}

/// The font weight/style selector [`Font::style_register`] keys a face by and [`Font::style_set`] activates.
/// FontStyle::Count is the size sentinel the C enum carries, not a style.
pub use crate::sys::FontStyle;

/// Mount-relative font and icon loading, over the same [`Mount`] handle the VFS operations borrow. A host
/// that serves its assets out of its own data mount reaches for these instead of the path-taking
/// [`Font::load`] / [`Icon::get`], which resolve against flowi's hidden working-directory asset mount.
impl Font {
    /// Start loading a TTF from relative_path within mount at default_size (unscaled px; 0 selects a
    /// built-in default). Returns the font id to poll with [`Font::is_loaded`], or a zero (invalid) handle.
    #[inline]
    pub fn load_mount(
        mount: &Mount,
        relative_path: impl AsRef<str>,
        default_size: u16,
    ) -> crate::sys::FontId {
        // SAFETY: the per-module opaque FlVfsMount cast, as in Image::load_mount - pointer-to-opaque,
        // not a reinterpretation - over a mount the &Mount borrow keeps live for the call, and a path the
        // VFS copies before returning.
        unsafe {
            crate::sys::fl_font_load_mount(
                mount.as_raw().cast(),
                crate::sys::RawStr::borrow(relative_path.as_ref()),
                default_size,
            )
        }
    }

    /// Register the face at relative_path within mount as the fallback flowi draws glyphs from when the
    /// primary font lacks them (typically Noto Sans, for full Latin Extended coverage). False on failure.
    #[inline]
    pub fn load_fallback(mount: &Mount, relative_path: impl AsRef<str>) -> bool {
        // SAFETY: the same per-module opaque FlVfsMount cast as Font::load_mount, over a mount held
        // live by the borrow and a path the VFS copies during the call.
        unsafe {
            crate::sys::fl_font_load_fallback(
                mount.as_raw().cast(),
                crate::sys::RawStr::borrow(relative_path.as_ref()),
            )
        }
    }
}

impl Icon {
    /// Load the standard toolkit icon set (checkmark, chevron, warning) from mount, so flowi's own chrome
    /// has images to draw. Fetch an individual one afterwards with [`Icon::get`].
    #[inline]
    pub fn load(mount: &Mount) {
        // SAFETY: the same per-module opaque FlVfsMount cast as Font::load_mount, over a mount the
        // borrow keeps live for the call.
        unsafe { crate::sys::fl_icon_load(mount.as_raw().cast()) };
    }
}

/// The resolved theme palette and its groups - handed to [`Theme::update`], returned by [`Theme::colors`].
/// [`ThemeOverride`] / [`ThemeSlot`] drive the sparse subtree override [`Theme::push`] takes.
pub use crate::sys::{
    ThemeBorder, ThemeColors, ThemeFeedback, ThemeInteractive, ThemeOverride, ThemeSlot,
    ThemeSurface, ThemeText,
};

/// The semantic action vocabulary - the logical verb a widget responds to, decoupled from the
/// physical input that triggers it. Consumed by [`action_pressed`].
pub use crate::sys::Action;

/// This frame's navigation snapshot, returned by [`nav_frame`].
pub use crate::sys::NavFrame;

/// The navigation-boundary bitmask a UI plugin's update returns - which nav edges the plugin reports having
/// reached, plus OVERRIDE for explicit edge control.
pub use crate::sys::NavEdge;

/// Vertical alignment of a text run within its line box - the typed view of TextConfig.valign.
/// Set [`TextConfig::valign`]; it defaults to
/// [`TextVAlign::Center`].
pub use crate::sys::TextVAlign;

/// How a single-line text run that overflows its box is handled - the typed view of TextConfig.overflow.
/// Set [`TextConfig::overflow`]; it defaults to
/// [`Overflow::Clip`] (hard-clip). [`Overflow::Elide`] truncates to the longest fitting
/// prefix and appends an ellipsis (U+2026), constraining the run to its box width.
pub use crate::sys::Overflow;

/// Defines RAII closers: hidden unit structs whose drop runs the given end call.
macro_rules! end_guards {
    ($($(#[$doc:meta])* $name:ident => $end:path;)+) => {
        $(
            $(#[$doc])*
            #[doc(hidden)]
            pub struct $name(());

            impl $name {
                #[allow(clippy::new_without_default)]
                pub fn new() -> $name {
                    $name(())
                }
            }

            impl Drop for $name {
                fn drop(&mut self) {
                    $end();
                }
            }
        )+
    };
}

end_guards! {
    /// RAII closer emitted by the layout-container macros ([`layout!`], [`row!`], [`col!`], [`placed!`],
    /// [`floating!`]): [`Layout::end`] runs on drop, so the container closes even when the body panics
    /// or returns early.
    LayoutEndGuard => Layout::end;

    /// RAII closer emitted by [`scroll!`]: [`Scroll::area_end`] runs on drop. See [`LayoutEndGuard`].
    ScrollEndGuard => Scroll::area_end;

    /// RAII closer emitted by [`menu!`]: [`Menu::end`] runs on drop. See [`LayoutEndGuard`].
    MenuEndGuard => Menu::end;

    /// RAII closer emitted by [`menu_region!`]: [`Menu::region_end`] runs on drop. See [`LayoutEndGuard`].
    MenuRegionEndGuard => Menu::region_end;

    /// RAII closer emitted by [`menubar!`]: [`MenuBar::end`] runs on drop. See [`LayoutEndGuard`].
    MenuBarEndGuard => MenuBar::end;

    /// RAII closer emitted by [`submenu!`]: [`UiSubmenu::end`] runs on drop. See [`LayoutEndGuard`].
    SubmenuEndGuard => UiSubmenu::end;

    /// RAII closer emitted by [`menubar_menu!`]: [`UiMenubarMenu::end`] runs on drop. See [`LayoutEndGuard`].
    MenubarMenuEndGuard => UiMenubarMenu::end;

    /// RAII closer emitted by [`dock_panel!`]: [`Dock::panel_end`] runs on drop. See [`LayoutEndGuard`].
    DockPanelEndGuard => Dock::panel_end;
}

/// Declare a Detachable Panel under its tab title, and emit its content into the tile the dock put it in.
///
/// ```ignore
/// dock_panel!(SYNTH_PANEL, "Synth", {
///     // ...the panel's widgets, laid out inside its dock tile...
/// });
/// ```
///
/// The title is copied into the panel's record; "" gives a blank tab.
///
/// The body runs only when the panel is visible this frame - the selected tab of the leaf holding it, or
/// floating - and a hidden panel keeps its slot reserved by virtue of having been declared.
///
/// Evaluates to whether the body ran, for the caller that has to do something when its panel stays hidden
/// (stop a preview, release a resource) rather than merely skip drawing.
#[macro_export]
macro_rules! dock_panel {
    ($id:expr, $title:expr, $body:block) => {
        if $crate::Dock::panel($id, $title) {
            let _dock_panel = $crate::DockPanelEndGuard::new();
            $body;
            true
        } else {
            false
        }
    };
}

/// Per-call styling for [`text_styled`]. Build one with ..Default::default() and set only the fields you
/// care about; [`Default`] is [`valign`](Self::valign) = [`TextVAlign::Center`] and every other field zero.
/// A zero field means "use the fallback" - [`font`](Self::font) None → the active font, [`font_size`](Self::font_size) 0 → the
/// font's default size, [`no_wrap`](Self::no_wrap) false → word wrap. [`color`](Self::color) has no
/// fallback: a zeroed colour is transparent, so set it for visible text.
#[derive(Copy, Clone, Debug)]
pub struct TextConfig {
    /// Font override; None uses the active font.
    pub font: Option<FontId>,
    /// Pixel size; 0 uses the font's default size.
    pub font_size: u16,
    /// Text colour. Zeroed (transparent) by default - set it for visible text.
    pub color: Color,
    /// Extra spacing between glyphs, in px.
    pub letter_spacing: u16,
    /// Line height in px; 0 uses the font metric.
    pub line_height: u16,
    /// Vertical alignment within the line box. Defaults to [`TextVAlign::Center`].
    pub valign: TextVAlign,
    /// Disable word wrap (single line) when true.
    pub no_wrap: bool,
    /// Single-line overflow handling. Defaults to [`Overflow::Clip`] (hard-clip); [`Overflow::Elide`] truncates
    /// to the longest fitting prefix and appends an ellipsis.
    pub overflow: Overflow,
}

impl Default for TextConfig {
    fn default() -> Self {
        TextConfig {
            font: None,
            font_size: 0,
            color: Color {
                r: 0,
                g: 0,
                b: 0,
                a: 0,
            },
            letter_spacing: 0,
            line_height: 0,
            valign: TextVAlign::Center,
            no_wrap: false,
            overflow: Overflow::Clip,
        }
    }
}

impl TextConfig {
    /// Lower to the raw TextConfig the seam takes. font: None becomes the C font_id = 0 (active font).
    fn to_sys(self) -> sys::TextConfig {
        sys::TextConfig {
            font_id: self.font.map_or(0, |f| f.0),
            font_size: self.font_size,
            color: self.color,
            letter_spacing: self.letter_spacing,
            line_height: self.line_height,
            valign: self.valign,
            no_wrap: self.no_wrap,
            overflow: self.overflow,
        }
    }
}

/// Emit a configurable layout-participating text element - the [`TextConfig`] form of
/// [`Text::styled`]. Like the plain [`Text::emit`], the run lays out as a normal element inside the
/// current container; unlike it, config carries per-call font, size, colour, spacing, vertical alignment, and
/// wrap control. Build config with [`TextConfig::default`] plus the fields you want (e.g. TextConfig { color,
/// valign, ..Default::default() }). Call inside a frame callback within a layout scope. Accepts any
/// `impl AsRef<str>`; the bytes are copied into the per-frame arena, so temporaries (e.g. a format! result) are
/// fine, and callers never construct a RawStr.
pub fn text_styled(text: impl AsRef<str>, config: TextConfig) {
    Text::styled(text, config.to_sys())
}

/// [`SliderConfig`], the [`Slider`] façade it belongs to, and the slider format-string validation.
mod slider;

/// The slider surface: the config, the façade, and the bare/_with sugar over it.
pub use slider::{
    slider_float, slider_float_with, slider_int, slider_int_with, Slider, SliderConfig,
};

/// An editable text field (inline rename, search box, any editable value). The handle closes on [`Drop`]; the field is driven with
/// [`clear`](TextInput::clear) / [`show`](TextInput::show) / [`set_text`](TextInput::set_text) /
/// [`text`](TextInput::text).
pub use generated::text_input::TextInput;

impl TextInput {
    /// Create a text input with a default initial buffer capacity. The backing arena and state are bootstrapped
    /// lazily, so a TextInput is usable without a live [`Application`] (the round-trip mutators below need no
    /// frame); only [`show`](Self::show) must run inside a frame callback.
    pub fn new() -> Self {
        Self::with_capacity(0)
    }

    /// Create a text input requesting capacity bytes of initial buffer (a built-in floor applies for small
    /// values; the buffer also grows on demand). Pass 0 for the built-in default.
    pub fn with_capacity(capacity: u32) -> Self {
        ensure_fl_init();
        // SAFETY: a plain call over a u32; the base subsystems it allocates from are up (just ensured).
        let handle = unsafe { sys::fl_text_input_create(capacity) };
        assert!(!handle.is_null(), "text_input_create failed");
        TextInput { handle }
    }
}

impl Default for TextInput {
    fn default() -> Self {
        Self::new()
    }
}

/// The identity of a layout element: a name plus an integer seed. The name is hashed with the seed to derive
/// the element's internal id, so the same name with different seeds yields
/// distinct elements. The id owns a copy of the name, so temporaries are fine and an id may be kept past its
/// frame; a per-frame copy is made each time the id is handed to an entry point. It is accepted
/// by every id-taking entry point ([`Layout::start`], [`Floating::start`], [`Scroll::area_begin`],
/// [`UiInput::scroll_target`], [`UiInput::mouse_wheel_delta`]) through `impl Into<LayoutId>`.
///
/// Construct it implicitly at the call site - never name the type:
/// - A bare string is seed 0, the common case: Layout::start("toolbar").
/// - A (name, seed) tuple carries a disambiguating seed: Layout::start(("lane", track_index)). The seed
///   accepts a usize (an .enumerate() counter), i32, or u32, each cast to the stored i32.
///
/// Sibling elements emitted from one source line in a loop (timeline lanes, list rows) all share the macro's
/// synthesised file:line:column id and would collide - a fatal duplicate-id assert in debug builds. The
/// seed disambiguates them without formatting a unique name per sibling. To disambiguate across more than one
/// axis (e.g. track × clip), pack the indices into the single i32 seed yourself - `("clip", (track << 16) | clip)`.
pub struct LayoutId {
    name: Box<str>,
    seed: i32,
}

impl LayoutId {
    fn new(name: &str, seed: i32) -> Self {
        LayoutId {
            name: name.into(),
            seed,
        }
    }

    /// The raw form the core takes, with the name copied into the per-frame arena. Built at the moment of use,
    /// so an id kept past its frame is re-copied into the live frame rather than handing the core a dangling
    /// pointer. Consuming self keeps the generated wrappers' `id.into().into_sys()` shape.
    pub(crate) fn into_sys(self) -> sys::LayoutId {
        sys::LayoutId {
            // SAFETY: the pointer/length pair describes the owned name's live bytes. fl_frame_str copies
            // them before returning, so the borrow only has to outlive this call; the copy is valid for the
            // rest of the current frame, which is as long as the core keeps it. It asserts an active frame,
            // and the id-taking entry points only run inside one.
            name: unsafe {
                sys::fl_frame_str(self.name.as_ptr() as *const c_char, self.name.len())
            },
            id: self.seed,
        }
    }
}

impl From<&str> for LayoutId {
    fn from(name: &str) -> Self {
        LayoutId::new(name, 0)
    }
}

impl From<&String> for LayoutId {
    fn from(name: &String) -> Self {
        LayoutId::new(name, 0)
    }
}

impl From<String> for LayoutId {
    fn from(name: String) -> Self {
        LayoutId {
            name: name.into_boxed_str(),
            seed: 0,
        }
    }
}

impl From<(&str, usize)> for LayoutId {
    fn from((name, seed): (&str, usize)) -> Self {
        LayoutId::new(name, seed as i32)
    }
}

impl From<(&str, i32)> for LayoutId {
    fn from((name, seed): (&str, i32)) -> Self {
        LayoutId::new(name, seed)
    }
}

impl From<(&str, u32)> for LayoutId {
    fn from((name, seed): (&str, u32)) -> Self {
        LayoutId::new(name, seed as i32)
    }
}

impl LayoutId {
    /// Resolve this id (name + seed) to the [`ElementId`] the core keys its state under - the same hash the
    /// element is laid out under, so an id you hash here equals the [`hot_element`] result when that element
    /// is hot. The hash is one-way; hash your own candidate ids and key a `{ElementId -> object}` map on the
    /// results to map a returned hot id back to its object. Build the LayoutId the usual way ("name".into() or
    /// a (name, seed) tuple) and call this on it.
    #[inline]
    pub fn hashed_id(self) -> ElementId {
        element_id(&self.name, self.seed)
    }

    /// The name this id was built from, read from the copy the id owns, so it stays valid for as long as the
    /// LayoutId lives, even past the frame it was built in.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// The disambiguating seed this id was built with - 0 for a bare-string id.
    pub fn seed(&self) -> i32 {
        self.seed
    }
}

/// Hash a (name, seed) pair into the [`ElementId`] the core keys that element's state under, without an
/// active frame. The out-of-frame counterpart to [`LayoutId::hashed_id`]: a [`LayoutId`] copies its name
/// into the per-frame arena when handed to an entry point and so may only be used inside a frame callback,
/// while this borrows name for the duration of the hash and touches no arena at all.
///
/// This is the identity to hold when a host polls or drives element state from a pass that runs outside the
/// frame - an update pass that decides what to show before rendering it. Pair it with [`element_state_id`] to
/// read that element's state and [`set_focus_id`] to move focus to it. Inside a frame, prefer the [`LayoutId`]
/// forms: they read the same, and the name stays visible at the call site.
///
/// The hash is the same one the element is laid out under, so an id from here compares equal to the
/// [`LayoutId::hashed_id`] of the same (name, seed) and to a matching [`hot_element`] result.
pub fn element_id(name: &str, seed: i32) -> ElementId {
    // SAFETY: the hash reads name's bytes during the call and keeps nothing, so a borrow that outlives the
    // call is enough - no frame arena, and no frame, is involved.
    unsafe {
        sys::fl_input_hashed_id(sys::LayoutId {
            name: sys::RawStr::borrow(name),
            id: seed,
        })
    }
}

/// When a scroll area shows its scrollbar - the value [`ScrollAreaConfig`]'s `scrollbar_mode` takes.
pub use crate::sys::ScrollbarDisplayMode;

/// The composing [`List`] methods, the value types they hand around, and the RAII [`SelectionSet`].
mod list;

/// The List/selection value types.
pub use list::{
    ListConfig, ListItem, ListOpts, ListResult, Reorder, SelectionRequest, SelectionSet,
};

/// The dynamic-image widget and the owned pixel buffer it draws.
mod dynamic_image;
pub use dynamic_image::{DynamicImage, DynamicImageBuffer};

/// An element's interaction-and-geometry state for this frame - the result of the [`element_state`] query and the
/// pull dual of a widget's pushed-back [`Signal`]. Keyed by an element id, so it serves a plain area (a timeline
/// viewport) as readily as a built-in widget. pos/size are in [`UiInput::mouse_position`]'s logical
/// (authoring) space, so size composes directly with cursor and content-transform math. The fields beyond hot
/// (focused, active, and the _t interpolators) are populated by the Widget Prologue, so a plain non-widget
/// area reports its rect / visible / hot / found and zeros for the rest.
#[derive(Copy, Clone, Debug)]
pub struct ElementState {
    /// Element origin in logical (authoring) px - the same space as [`UiInput::mouse_position`].
    pub pos: Vec2,
    /// Element size in logical (authoring) px.
    pub size: Vec2,
    /// Hover animation phase, 0..1.
    pub hot_t: f32,
    /// Active/press animation phase, 0..1.
    pub active_t: f32,
    /// Focus animation phase, 0..1.
    pub focused_t: f32,
    /// The element was present in this frame's layout state - when false, every other field is zero/false.
    pub found: bool,
    /// The element is within the visible/clip region.
    pub visible: bool,
    /// The cursor is over this element (flowi's own hit-test).
    pub hot: bool,
    /// The element holds keyboard/gamepad focus.
    pub focused: bool,
    /// The element is being pressed/activated.
    pub active: bool,
}

/// Read the element id's [`ElementState`] for this frame - its rect, layout presence, and interaction. The dual
/// of a widget's [`Signal`]: a Signal is handed back by value from a widget you called, Element State is
/// pulled by id from any element you laid out, so it works for a plain area as well as a widget. The pos/size
/// rect is normalised to logical (authoring) px - the same space as [`UiInput::mouse_position`] and the
/// Painter - by dividing out the absolute UI scale, so the app never handles physical pixels or ui_scale itself.
/// The rect is the previous frame's resolved layout (immediate mode), so query an element that existed last
/// frame; a never-laid-out id returns found = false with a zero rect. Note a plain area does not enter the
/// interaction state on its own - a widget enters via its own behavior, but a bare container must opt in by
/// declaring itself a [`UiInput::scroll_target`] (the timeline-viewport pattern); until it does, element_state
/// reports found = false for it.
///
/// # Panics
/// Panics when called off the thread that owns the live [`Application`].
pub fn element_state(id: impl Into<LayoutId>) -> ElementState {
    let id: LayoutId = id.into();
    let _frame = frame_thread::frame_thread_guard("element_state");
    // SAFETY: into_sys copies the LayoutId's name into the live frame arena just before this call, so it is
    // live for it; the query reads it and returns a Copy struct; _frame keeps the call to the frame
    // context's thread.
    element_state_raw(unsafe { sys::fl_input_get_element_state(id.into_sys()) })
}

/// Read the [`ElementState`] of an already-hashed [`ElementId`] - the same query [`element_state`] performs,
/// keyed by the hashed id instead of the [`LayoutId`] it comes from, and normalised to logical px the same way.
///
/// This is the form to use from outside a frame: handing a [`LayoutId`] to the core copies its name into the
/// per-frame arena, which only exists between the frame's begin and end, so a host polling element state from its update
/// pass reaches for [`element_id`] plus this. Inside a frame the two are interchangeable and [`element_state`]
/// reads better.
///
/// # Panics
/// Panics when called off the thread that owns the live [`Application`].
pub fn element_state_id(id: ElementId) -> ElementState {
    let _frame = frame_thread::frame_thread_guard("element_state_id");
    // SAFETY: a plain call over an already-hashed integer id; no pointer crosses, and no frame is required.
    element_state_raw(unsafe { sys::fl_input_get_element_state_by_id(id) })
}

impl Default for ElementState {
    /// The state of an element that was never laid out: found = false, an empty rect, and every phase at rest -
    /// exactly what [`element_state`] reports for an id this frame has not seen.
    fn default() -> ElementState {
        ElementState {
            pos: Vec2 { x: 0.0, y: 0.0 },
            size: Vec2 { x: 0.0, y: 0.0 },
            hot_t: 0.0,
            active_t: 0.0,
            focused_t: 0.0,
            found: false,
            visible: false,
            hot: false,
            focused: false,
            active: false,
        }
    }
}

/// Normalise a raw ElementState - whose rect is physical px - into the logical (authoring) px the public
/// [`ElementState`] reports.
fn element_state_raw(s: sys::ElementState) -> ElementState {
    // SAFETY: an argument-free read of the active context's scale; no pointer crosses.
    let scale = unsafe { sys::fl_input_get_scale() };
    let inv = if scale != 0.0 { 1.0 / scale } else { 0.0 };
    ElementState {
        pos: Vec2 {
            x: s.pos.x * inv,
            y: s.pos.y * inv,
        },
        size: Vec2 {
            x: s.size.x * inv,
            y: s.size.y * inv,
        },
        hot_t: s.hot_t,
        active_t: s.active_t,
        focused_t: s.focused_t,
        found: s.found,
        visible: s.visible,
        hot: s.hot,
        focused: s.focused,
        active: s.active,
    }
}

/// The single topmost-hovered element across the whole frame - the core's already-arbitrated hot id, or None
/// when nothing is hovered. The pull complement for a consumer that handles input in a pass separate from
/// declaration: it answers "which element is under the cursor?" in one read, without re-walking every id calling
/// [`element_state`]. Overlapping elements resolve topmost-wins (the front one is reported). Map the returned
/// [`ElementId`] back to your own object by hashing your candidate ids with [`LayoutId::hashed_id`] into a
/// `{ElementId -> object}` map. The inline model - read hovered/pressed from each element's own [`Signal`] -
/// needs neither this nor a re-walk, and is preferred where it fits.
///
/// # Panics
/// Panics when called off the thread that owns the live [`Application`].
pub fn hot_element() -> Option<ElementId> {
    let _frame = frame_thread::frame_thread_guard("hot_element");
    // SAFETY: an argument-free read of this frame's arbitrated hot id; no pointer crosses.
    let id = unsafe { sys::fl_input_hot_id() };
    (id != ElementId::INVALID).then_some(id)
}

/// This frame's navigation snapshot: who holds focus, plus which directional nav is pending. Read it once per
/// frame and drive focus/scroll decisions from it, rather than polling the individual `nav_*_pressed` queries.
///
/// # Panics
/// Panics when called off the thread that owns the live [`Application`].
#[inline]
pub fn nav_frame() -> NavFrame {
    let _frame = frame_thread::frame_thread_guard("nav_frame");
    // SAFETY: an argument-free read of the active context's nav state; no pointer crosses.
    unsafe { sys::fl_get_nav_frame() }
}

/// True if action was pressed this frame. Context-free: reads the active UI context's per-frame action
/// state. Which physical input maps onto each action is app-level binding configuration, not part of this
/// surface.
///
/// # Panics
/// Panics when called off the thread that owns the live [`Application`].
#[inline]
pub fn action_pressed(action: Action) -> bool {
    let _frame = frame_thread::frame_thread_guard("action_pressed");
    // SAFETY: a plain call over a Copy enum; no pointer crosses.
    unsafe { sys::fl_action_pressed(action) }
}

/// True if navigation up was pressed this frame (arrow, D-pad, or left stick).
///
/// # Panics
/// Panics when called off the thread that owns the live [`Application`].
#[inline]
pub fn nav_up_pressed() -> bool {
    let _frame = frame_thread::frame_thread_guard("nav_up_pressed");
    // SAFETY: an argument-free read of this frame's nav state; no pointer crosses.
    unsafe { sys::fl_nav_up_pressed() }
}

/// True if navigation down was pressed this frame (arrow, D-pad, or left stick).
///
/// # Panics
/// Panics when called off the thread that owns the live [`Application`].
#[inline]
pub fn nav_down_pressed() -> bool {
    let _frame = frame_thread::frame_thread_guard("nav_down_pressed");
    // SAFETY: an argument-free read of this frame's nav state; no pointer crosses.
    unsafe { sys::fl_nav_down_pressed() }
}

/// Consume a pending navigation direction so the focus system won't also act on it this frame. Call when the
/// widget handles the direction itself - the canonical case is a list that scrolls internally until it reaches
/// its boundary, and only then lets focus move on.
///
/// # Panics
/// Panics when called off the thread that owns the live [`Application`].
#[inline]
pub fn consume_nav_up() {
    let _frame = frame_thread::frame_thread_guard("consume_nav_up");
    // SAFETY: an argument-free mutation of this frame's nav state; no pointer crosses.
    unsafe { sys::fl_consume_nav_up() }
}

/// Consume a pending nav-down. See [`consume_nav_up`].
///
/// # Panics
/// Panics when called off the thread that owns the live [`Application`].
#[inline]
pub fn consume_nav_down() {
    let _frame = frame_thread::frame_thread_guard("consume_nav_down");
    // SAFETY: an argument-free mutation of this frame's nav state; no pointer crosses.
    unsafe { sys::fl_consume_nav_down() }
}

/// Consume a pending nav-left. See [`consume_nav_up`].
///
/// # Panics
/// Panics when called off the thread that owns the live [`Application`].
#[inline]
pub fn consume_nav_left() {
    let _frame = frame_thread::frame_thread_guard("consume_nav_left");
    // SAFETY: an argument-free mutation of this frame's nav state; no pointer crosses.
    unsafe { sys::fl_consume_nav_left() }
}

/// Consume a pending nav-right. See [`consume_nav_up`].
///
/// # Panics
/// Panics when called off the thread that owns the live [`Application`].
#[inline]
pub fn consume_nav_right() {
    let _frame = frame_thread::frame_thread_guard("consume_nav_right");
    // SAFETY: an argument-free mutation of this frame's nav state; no pointer crosses.
    unsafe { sys::fl_consume_nav_right() }
}

/// Programmatically move focus to the widget identified by id - e.g. restoring focus when navigating back to
/// a page. Takes the same ergonomic [`LayoutId`] the layout macros and [`Item::behavior`] accept.
///
/// # Panics
/// Panics when called off the thread that owns the live [`Application`].
#[inline]
pub fn set_focus(id: impl Into<LayoutId>) {
    let id: LayoutId = id.into();
    let _frame = frame_thread::frame_thread_guard("set_focus");
    // SAFETY: into_sys copies the LayoutId's name into the live frame arena just before this call, so it is
    // live for it; the core hashes it and stores the resulting integer id.
    unsafe { sys::fl_set_focus(id.into_sys()) }
}

/// Programmatically move focus to an already-hashed [`ElementId`] - the form [`hot_element`] and
/// [`LayoutId::hashed_id`] hand out. Prefer [`set_focus`] when the [`LayoutId`] is available, since that is the
/// stable cross-frame identity; this is for callers that only hold the within-frame id.
///
/// # Panics
/// Panics when called off the thread that owns the live [`Application`].
#[inline]
pub fn set_focus_id(id: ElementId) {
    let _frame = frame_thread::frame_thread_guard("set_focus_id");
    // SAFETY: a plain call over an already-hashed integer id; no pointer crosses.
    unsafe { sys::fl_set_focus_id(id) }
}

impl Theme {
    /// The active theme palette flowi is painting with. The read complement of [`Theme::update`] - a widget
    /// deriving its own colors (a selection tint, a perceptually-adjusted hover) starts from these rather than
    /// hard-coding a palette.
    ///
    /// # Panics
    /// Panics when called off the thread that owns the live [`Application`].
    #[inline]
    pub fn colors() -> ThemeColors {
        let _frame = frame_thread::frame_thread_guard("Theme::colors");
        // SAFETY: flowi always has a resolved palette, so fl_theme_colors returns a valid, aligned pointer
        // to it; the read copies the Copy struct out and the pointer does not escape. _frame keeps
        // Theme::update from overwriting the palette during the read.
        unsafe { sys::fl_theme_colors().read() }
    }
}

/// Render backend selection for [`Application::with_renderer`].
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Renderer {
    /// Software renderer - offscreen RGB24, always available. The default for [`Application::new`].
    Software,
    /// GPU compute backend - offscreen RGBA8, headless EGL-surfaceless or a windowed SDL-GL surface. Available
    /// only in the GPU build and needs a live GL 4.3 context.
    Gpu,
}

impl Renderer {
    fn to_sys(self) -> crate::sys::AppRenderer {
        match self {
            Renderer::Software => crate::sys::AppRenderer::Software,
            Renderer::Gpu => crate::sys::AppRenderer::Gpu,
        }
    }
}

/// A mouse button - the selector for the [`mouse_button`] query, the [`UiInput::mouse_pressed`] /
/// [`UiInput::mouse_down`] / [`UiInput::mouse_released`] slots, and the headless injection harness
/// ([`Application::inject_mouse_button`]). [`MouseButton::Count`] is the array-size sentinel the C enum
/// carries, not a button.
pub use crate::sys::MouseButton;

/// A mouse button's interaction over one frame, the three channels of the [`mouse_button`] query. The
/// channels are not mutually exclusive: a fast tap is
/// pressed && released in the same frame, and a just-pressed button is both pressed and held.
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub struct MouseButtonState {
    /// The button went down this frame (the down edge).
    pub pressed: bool,
    /// The button is currently down (the held level).
    pub held: bool,
    /// The button went up this frame (the up edge).
    pub released: bool,
}

/// Read button's state this frame - global and unrouted, the three channels bundled into a
/// [`MouseButtonState`]. Every reader sees the same state regardless of what is under the cursor: buttons are
/// never zeroed or re-routed mid-frame (unlike [`UiInput::mouse_wheel_delta`], which is scoped to a
/// [`UiInput::scroll_target`]). An owned interaction (a drag) is the app's to latch: note
/// [`MouseButtonState::pressed`] while the cursor is over your region (test it against
/// [`UiInput::mouse_position`]), then follow [`MouseButtonState::held`] and the cursor until
/// [`MouseButtonState::released`], even as the cursor leaves the region.
pub fn mouse_button(button: MouseButton) -> MouseButtonState {
    MouseButtonState {
        pressed: UiInput::mouse_pressed(button),
        held: UiInput::mouse_down(button),
        released: UiInput::mouse_released(button),
    }
}

/// A keyboard key. The variant names drop the C FlKey_ prefix: FlKey_Delete is
/// [`Key::Delete`], FlKey_Escape is
/// [`Key::Escape`]. The main number-row keys are Num0..Num9 (the keypad is Kp_0..Kp_9). Read a key's state
/// for the current frame through the generated [`UiInput::key_pressed`] / [`UiInput::key_down`] /
/// [`UiInput::key_released`] façade methods, e.g. UiInput::key_pressed(Key::Delete); inject one in a headless
/// test with [`Application::inject_key`].
pub use crate::sys::Key;

/// A system cursor shape - the selector for [`Cursor::system`].
pub use crate::sys::CursorShape;

impl Cursor {
    /// A built-in system cursor of the given [`CursorShape`], ready to hand to [`Cursor::set`].
    pub fn system(shape: CursorShape) -> sys::Cursor {
        sys::Cursor { shape }
    }
}

/// Per-item close policy on menu-item activation - the typed view of [`MenuItemConfig::close_policy`].
/// [`MenuClosePolicy::Default`] (the zero value)
/// lets each item kind use its natural default (an action item closes the chain on activation);
/// [`MenuClosePolicy::Close`] / [`MenuClosePolicy::KeepOpen`] force the exception either way.
pub use crate::sys::MenuClosePolicy;

/// Per-call configuration for a context menu panel. Build
/// one with ..Default::default() and set only what you need; a zero field means "use the default"
/// ([`min_width`](MenuConfig::min_width) 0.0 → 180 px, [`z_index`](MenuConfig::z_index) 0 → 1000, above
/// popups), so the binding does no per-field defaulting. Hand to [`Menu::begin`] or set fields through the
/// [`menu!`] macro.
pub use generated::menu::MenuConfig;

/// Per-call configuration for a menu action item; shortcut is a plain &str copied into the per-frame
/// arena at the call. Build one with ..Default::default() and set only what you need; a zero field means "use the default"
/// ([`close_policy`](MenuItemConfig::close_policy) [`MenuClosePolicy::Default`], an empty
/// [`shortcut`](MenuItemConfig::shortcut), not [`disabled`](MenuItemConfig::disabled)).
/// [`shortcut`](MenuItemConfig::shortcut) is display-only right-aligned text (no dispatch). Hand to
/// [`menu_item_with`] / [`Menu::item`] or set fields through the [`menu_item!`] macro.
pub use generated::menu::MenuItemConfig;

/// Per-call configuration for a submenu parent. A submenu
/// parent is a pure container, so unlike [`MenuItemConfig`] it carries no close policy or shortcut: only the
/// child panel's [`min_width`](SubmenuConfig::min_width) and a [`disabled`](SubmenuConfig::disabled) flag.
/// Build one with ..Default::default(); a zero field means "use the default"
/// ([`min_width`](SubmenuConfig::min_width) 0.0 → 180). Hand to [`UiSubmenu::begin`] or set fields through
/// the [`submenu!`](crate::submenu) macro.
pub use generated::menu::SubmenuConfig;

/// A zeroed MenuRegionConfig, which the core reads as the default grow/grow region covering the parent area.
fn region_default_config() -> sys::MenuRegionConfig {
    let axis = LayoutSizingAxis {
        size: LayoutSizingAxisSize {
            min_max: LayoutSizingMinMax { min: 0.0, max: 0.0 },
        },
        r#type: LayoutSizingType::Fit,
    };
    sys::MenuRegionConfig {
        sizing: LayoutSizing {
            width: axis,
            height: axis,
        },
    }
}

impl Menu {
    /// Begin a right-click region region_id that opens the menu menu_id at the cursor on a right-click inside.
    /// Returns the region's interaction [`Signal`] (read [`Signal::right_clicked`](Signal#structfield.right_clicked)
    /// if you want to react to the click yourself; opening is automatic). The region defaults to a grow/grow
    /// catch-all covering the parent area. Pair with [`Menu::region_end`]; the [`menu_region!`] macro does this for
    /// you. Each id is any [`LayoutId`].
    ///
    /// # Panics
    /// Panics when called off the thread that owns the live [`Application`].
    pub fn region_begin(region_id: impl Into<LayoutId>, menu_id: impl Into<LayoutId>) -> Signal {
        let region_id: LayoutId = region_id.into();
        let menu_id: LayoutId = menu_id.into();
        let _frame = frame_thread::frame_thread_guard("Menu::region_begin");
        // SAFETY: into_sys copies both LayoutId names into the live frame arena just before this call, so they
        // are live for it; the config is a Copy struct of scalars; _frame keeps the call to the frame
        // context's thread.
        unsafe {
            sys::fl_menu_region_begin_impl(
                region_id.into_sys(),
                menu_id.into_sys(),
                region_default_config(),
            )
        }
    }
}

/// Emit one menu action item labelled label inside an open [`menu!`] / [`Menu::begin`] scope, returning its
/// interaction [`Signal`] - read [`Signal::clicked`](Signal#structfield.clicked) the frame it activates. A plain
/// function so it reads naturally inline (if menu_item("Cut").clicked { ... }) and arbitrary widgets can sit
/// between items. Uses the default [`MenuItemConfig`]; for a shortcut, disabled state, or an explicit close policy
/// use [`menu_item_with`]. Accepts any `impl AsRef<str>`, copied into the per-frame arena.
pub fn menu_item(label: impl AsRef<str>) -> Signal {
    menu_item_with(label, MenuItemConfig::default())
}

/// Emit a menu action item with an explicit [`MenuItemConfig`] (shortcut text, disabled state, or close policy) -
/// the config form of [`Menu::item`]. See [`menu_item`] for the contract.
pub fn menu_item_with(label: impl AsRef<str>, config: MenuItemConfig) -> Signal {
    Menu::item(label, config)
}

/// Emit a checkable menu item labelled label bound to checked: clicking it toggles the bool (and shows a
/// check marker while it is true), keeping the menu open by default so several toggles can be flipped without
/// reopening. Returns its interaction [`Signal`] - read [`Signal::clicked`](Signal#structfield.clicked) the
/// frame it toggles. Uses the default [`MenuItemConfig`]; for a shortcut, disabled state, or an explicit close
/// policy use [`menu_check_with`]. Call inside an open [`menu!`] / [`Menu::begin`] scope.
pub fn menu_check(label: impl AsRef<str>, checked: &mut bool) -> Signal {
    menu_check_with(label, checked, MenuItemConfig::default())
}

/// Emit a checkable menu item with an explicit [`MenuItemConfig`]. See [`menu_check`] for the contract.
pub fn menu_check_with(
    label: impl AsRef<str>,
    checked: &mut bool,
    config: MenuItemConfig,
) -> Signal {
    Menu::check(label, checked, config)
}

/// Emit a radio menu item labelled label: one option in a group whose selection state is the caller's
/// selected. Clicking it sets `*selected = value`; the bullet marker shows while `*selected == value`, so a
/// group of radio items sharing one selected is mutually exclusive for free. Keeps the menu open by default
/// (same as [`menu_check`]). Returns its interaction [`Signal`]. Uses the default [`MenuItemConfig`]; for a
/// shortcut, disabled state, or an explicit close policy use [`menu_radio_with`].
pub fn menu_radio(label: impl AsRef<str>, selected: &mut i32, value: i32) -> Signal {
    menu_radio_with(label, selected, value, MenuItemConfig::default())
}

/// Emit a radio menu item with an explicit [`MenuItemConfig`]. See [`menu_radio`] for the contract.
pub fn menu_radio_with(
    label: impl AsRef<str>,
    selected: &mut i32,
    value: i32,
    config: MenuItemConfig,
) -> Signal {
    Menu::radio(label, selected, value, config)
}

/// Per-call configuration for the menu-bar strip. The strip
/// carries only the dropdowns' render order; the per-dropdown width lives on [`MenuBarMenuConfig`]. Build one
/// with ..Default::default(); a zero field means "use the default" ([`z_index`](MenuBarConfig::z_index)
/// 0 → 1000, same as a context menu). Hand to [`MenuBar::begin`] or set fields through the
/// [`menubar!`](crate::menubar) macro.
pub use generated::menu::MenuBarConfig;

/// Per-call configuration for one menu-bar top. Like a
/// [`SubmenuConfig`] a top is a pure container, so it carries no close policy or shortcut: only the dropdown
/// panel's [`min_width`](MenuBarMenuConfig::min_width) and a [`disabled`](MenuBarMenuConfig::disabled) flag.
/// Build one with ..Default::default(); a zero field means "use the default"
/// ([`min_width`](MenuBarMenuConfig::min_width) 0.0 → 180, same as a context menu). Hand to
/// [`UiMenubarMenu::begin`] or set fields through the [`menubar_menu!`](crate::menubar_menu) macro.
pub use generated::menu::MenuBarMenuConfig;

/// How a [`Popup`] scope is positioned - the typed view of [`PopupConfig::anchor`].
/// [`PopupAnchor::Element`] (the zero default) anchors to the element named by
/// [`Popup::begin`]'s anchor_id; [`PopupAnchor::Point`] anchors to the absolute [`PopupConfig::point`].
pub use crate::sys::PopupAnchor;

/// Per-scope configuration for a [`Popup::begin`] scope (offset, z-order, input capture, dismissal,
/// anchoring). A zeroed config is the default; set the
/// fields you need and hand it to [`Popup::begin`].
pub use generated::popup::PopupConfig;

/// Configuration for a [`Popup::tooltip`] (hover-dwell delay, placement offset, z-order).
pub use generated::popup::TooltipConfig;

/// Configuration for a [`Popup::message_box`] (button labels, panel width, z-order); both labels are plain
/// &str copied into the per-frame arena at the call. An empty
/// [`cancel_label`](MessageBoxConfig::cancel_label) is a
/// single-button alert; a set one is a two-button confirm. An empty
/// [`confirm_label`](MessageBoxConfig::confirm_label) takes the default ("OK").
pub use generated::popup::MessageBoxConfig;

/// The one-shot outcome of a [`Popup::message_box`]. [`DialogResult::None`] (the zero default) means still
/// open / no decision this frame; [`DialogResult::Confirmed`] / [`DialogResult::Cancelled`] are returned for
/// the single frame of dismissal.
pub use crate::sys::DialogResult;

/// Bring flowi's process-wide foundation up ([`init`]) and take it down ([`shutdown`]).
/// Every flowi entry point brings it up for itself, so a host that only ever reaches flowi
/// through an [`Application`] never calls either.
pub use crate::foundation::{init, is_up, shutdown};

/// Bring the foundation up for a surface that needs it, with the one job worker thread
/// async image decode wants. The count is honoured only by whichever call actually brings
/// the foundation up.
pub(crate) fn ensure_fl_init() -> crate::foundation::BaseArena {
    crate::foundation::init(1)
}

/// The single frame-driving thread and the entry points bound to it.
mod frame_thread;

/// The [`Application`] lifecycle, window-owning and embedded.
mod application;

/// The application surface: the one application type, its settings, the per-window handle, and the
/// frame snapshot [`Application::pixels`] returns.
pub use application::{
    AppSettings, Application, EmbeddedSettings, PixelView, Window, WindowSettings,
};

#[cfg(test)]
mod tests {
    /// No generated façade signature may take a slice of the raw RawStr ABI mirror: it has no safe
    /// constructor, so such a parameter is unreachable from this crate and forces the caller into a direct
    /// flowi_sys dependency. The generator lowers string arrays to `&[impl AsRef<str>]` instead and packs
    /// the mirror out of the frame arena internally.
    #[test]
    fn no_generated_signature_takes_a_raw_str_slice() {
        // The three string-array slots.
        for (name, src) in [
            ("dropdown", include_str!("generated/dropdown.rs")),
            ("tab_bar", include_str!("generated/tab_bar.rs")),
            ("radio_group", include_str!("generated/radio_group.rs")),
        ] {
            for line in src.lines().filter(|l| l.contains("pub fn ")) {
                assert!(
                    !line.contains("RawStr]"),
                    "{name}: generated signature exposes the raw RawStr mirror: {line}"
                );
            }
            assert!(
                src.contains("impl AsRef<str>]"),
                "{name}: expected a string-array parameter lowered to &[impl AsRef<str>]"
            );
        }
    }

    /// No generated façade signature may take the raw WidgetConfig ABI mirror: its id_override is a
    /// RawStr with no safe constructor, so such a parameter makes every non-default widget config
    /// unreachable without a direct flowi_sys dependency. The generator lowers it to the safe twin
    /// (crate::generated::widget_config::WidgetConfig) instead, which to_sys packs at the call.
    #[test]
    fn no_generated_signature_takes_the_raw_widget_config() {
        // Every def whose slots take a WidgetConfig.
        for (name, src) in [
            ("button", include_str!("generated/button.rs")),
            ("chip", include_str!("generated/chip.rs")),
            ("pill", include_str!("generated/pill.rs")),
        ] {
            for line in src.lines().filter(|l| l.contains("pub fn ")) {
                assert!(
                    !line.contains("crate::sys::WidgetConfig"),
                    "{name}: generated signature exposes the raw WidgetConfig mirror: {line}"
                );
            }
            assert!(
                src.contains("widget_config::WidgetConfig<'_>"),
                "{name}: expected the config parameter lowered to the safe mirror"
            );
        }
    }

    /// The C rule read back through the wrapper, including its two corners.
    #[test]
    fn natural_cmp_matches_the_c_comparator() {
        use crate::{natural_cmp, natural_cmp_bytes};
        use std::cmp::Ordering;

        // The headline rule: a digit run compares by value, not lexically.
        assert_eq!(natural_cmp("Disk2", "Disk10"), Ordering::Less);
        assert_eq!(natural_cmp("Disk10", "Disk2"), Ordering::Greater);
        assert_eq!(natural_cmp("track07", "track7"), Ordering::Equal);

        // Non-digits compare case-insensitively over ASCII.
        assert_eq!(natural_cmp("Alpha", "alpha"), Ordering::Equal);
        assert_eq!(natural_cmp("a", "B"), Ordering::Less);

        // A shared prefix orders the shorter string first.
        assert_eq!(natural_cmp("song", "song2"), Ordering::Less);
        assert_eq!(natural_cmp("", "a"), Ordering::Less);
        assert_eq!(natural_cmp("", ""), Ordering::Equal);

        // First corner: leading zeros are skipped before the runs are measured, so the remaining digit
        // count decides - 9 vs 09 is a tie, not a difference.
        assert_eq!(natural_cmp("file9", "file09"), Ordering::Equal);
        assert_eq!(natural_cmp("file9", "file10"), Ordering::Less);

        // Second corner: the C compares char, which is signed on the target ABI, so a byte >= 0x80 orders
        // as negative - below every ASCII character rather than above it.
        assert_eq!(natural_cmp_bytes(b"\xff", b"a"), Ordering::Less);
        assert_eq!(natural_cmp_bytes(b"a", b"\xff"), Ordering::Greater);

        // Bytes and strings are the same comparison; the byte form is what a non-UTF-8 pool needs.
        assert_eq!(natural_cmp_bytes(b"Disk2", b"Disk10"), Ordering::Less);
        assert_eq!(
            natural_cmp_bytes("Alpha".as_bytes(), "alpha".as_bytes()),
            natural_cmp("Alpha", "alpha")
        );
    }

    /// Image::from_pixels validates the buffer against width × height × bpp before the pointer crosses.
    /// The success path needs a live image system, so only the rejections are unit-testable here.
    #[test]
    fn from_pixels_rejects_buffers_that_do_not_match_the_declared_shape() {
        use crate::{Image, ImageFormat};

        let cases: [(&str, Vec<u8>, i32, i32, ImageFormat); 6] = [
            // One byte short / one byte long of the exact 4x2 RGBA extent.
            (
                "rgba short",
                vec![0; 4 * 2 * 4 - 1],
                4,
                2,
                ImageFormat::RGBA,
            ),
            ("rgba long", vec![0; 4 * 2 * 4 + 1], 4, 2, ImageFormat::RGBA),
            // The per-format byte widths: RGB is 3, Alpha is 1, XRGB is 4. Passing an RGBA-sized buffer for
            // an RGB image is the classic mistake, and it must not be read as a taller image.
            (
                "rgb sized as rgba",
                vec![0; 4 * 2 * 4],
                4,
                2,
                ImageFormat::RGB,
            ),
            (
                "alpha sized as rgb",
                vec![0; 4 * 2 * 3],
                4,
                2,
                ImageFormat::Alpha,
            ),
            (
                "xrgb sized as rgb",
                vec![0; 4 * 2 * 3],
                4,
                2,
                ImageFormat::XRGB,
            ),
            // A negative size has no valid extent at all.
            ("negative width", vec![0; 8], -1, 2, ImageFormat::RGBA),
        ];

        for (name, pixels, width, height, format) in cases {
            let result =
                std::panic::catch_unwind(|| Image::from_pixels(&pixels, width, height, format));
            assert!(result.is_err(), "{name}: should have been rejected");
        }
    }

    /// The two formats the raw upload path does not accept have no byte width to validate against, so they
    /// are rejected by name rather than silently uploading garbage.
    #[test]
    #[should_panic(expected = "does not accept")]
    fn from_pixels_rejects_a_format_the_upload_path_cannot_take() {
        let _ = crate::Image::from_pixels(&[0; 32], 4, 2, crate::ImageFormat::Deinterleaved);
    }
}
