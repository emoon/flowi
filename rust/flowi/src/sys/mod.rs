//! The raw C ABI of libflowi: foundation, VFS and toolkit, exactly as C declares it.
//!
//! The Rust spelling drops the C `Fl` prefix (`FlColor` -> `Color`). [`RawStr`] is the mirror
//! of the C string view `FlString`. Every generated tree names the types of the others through
//! this module's flat re-exports.
//!
//! Everything here is unsafe by nature; the crate's public API wraps it.
//!
//! # Features
//!
//! `global-allocator` (off by default) installs Flowi's native heap as the process-wide
//! Rust `#[global_allocator]`, so Rust and C allocations share one heap. Leave it off
//! when the final application supplies its own global allocator.

#![allow(non_camel_case_types)]
// These bindings mirror the full C API surface; not every item is consumed from Rust, so the
// unused ones are expected, not a defect.
#![allow(dead_code, unused_imports)]

use std::os::raw::{c_char, c_int, c_void};

mod foundation;
mod toolkit;
mod vfs_abi;

#[cfg(feature = "allocation-probe")]
pub mod allocation_probe;
pub mod allocator;

#[cfg(feature = "global-allocator")]
#[global_allocator]
static ALLOCATOR: allocator::HeapAllocator = allocator::HeapAllocator;

/// Panic containment for cdylibs: an extern "C" entry wrapped in [`ffi_guard`] logs a Rust
/// panic, poisons the plugin, and returns the slot's error value instead of unwinding across
/// the C ABI. See [`guard`] for the full contract.
pub mod guard;
pub use guard::{clear_poison, ffi_guard, is_poisoned, poison, set_panic_logger};

mod marshal;
pub use marshal::{arena_alloc_array, arena_box, borrow_slice, borrow_slice_mut};

mod raw_str;
pub use raw_str::RawStr;

/// The `arena_*` call API: the opaque [`Arena`] (an uninhabited enum, crossed only behind
/// `*mut Arena`), the [`TempArena`] savepoint a temp/scratch scope restores from, the
/// allocate/rewind/scratch entry points, and the process-wide `fl_init` / `fl_destroy`
/// foundation lifecycle.
pub use foundation::arena::*;
/// The file-watcher API: the FileChangeType bitflags, the watch config, the change record
/// and the arena-allocated batch fl_file_watcher_take_changes returns, plus the
/// `fl_file_watcher_*` entry points.
pub use foundation::file_watcher::*;
/// The `fl_heap_*` call API: the native process heap. Backs [`allocator::HeapAllocator`].
pub use foundation::heap::*;
/// The `fl_jobs_*` call API: the JobHandle handle, the JobsWorkerInfo POD, the JobsFunc
/// callback alias, the JobsResult / JobPriority enums, and the `fl_jobs_*` functions.
pub use foundation::jobs::*;
/// The `fl_json_*` call API: the JSON DOM structs (JsonValue / JsonObjectElement /
/// JsonArrayElement plus JsonObject / JsonArray / JsonNumber / JsonParseResult), the
/// JsonType / JsonParseError enums, and the `fl_json_*` functions.
pub use foundation::json::*;
/// The `fl_log_*` call API: the LogLevel severity enum plus the channel
/// register/level/enable, file-logging and set-level/flush functions. The variadic C
/// emitters have no binding; fl_log_c_message_formatted takes pre-formatted text.
pub use foundation::log::*;
/// The shared value types [`Color`] and [`Vec2`].
pub use foundation::math_data::*;
/// The `fl_memory_tracker_*` call API: the POD MemoryStats snapshot (arena usage and the
/// heap alloc/free balance) plus the init/destroy/dump_*/export_json_*/get_stats functions.
pub use foundation::memory_tracker::*;
/// The `path_*` manipulation functions, each taking the arena its result is allocated from.
pub use foundation::path::*;
/// The frame-profiler call API (`fl_perf_scope_*`): a host that owns the frame loop brackets
/// it with frame_begin/frame_end and opens named scopes inside.
pub use foundation::perf_scope::*;
/// The status vocabulary: the general-purpose Status code and [`FlError`], the shared error
/// enum fallible calls return.
pub use foundation::status::*;
/// The `string_*` / `sb_*` call API: the StringBuilder, the parse-result PODs and
/// StringParseStatus, and the comparison, search and hashing (FNV-1a) functions over
/// [`RawStr`].
pub use foundation::string::*;


/// The exported `vfs_*` service functions a directly linked consumer calls.
pub use vfs_abi::vfs::{
    fl_vfs_close, fl_vfs_get_data, fl_vfs_get_file_list, fl_vfs_get_listing, fl_vfs_is_ready,
    fl_vfs_mount_close, fl_vfs_mount_get_info, fl_vfs_mount_get_status, fl_vfs_mount_is_ready,
    fl_vfs_mount_open, fl_vfs_mount_read_all_with_options, fl_vfs_mount_with_options, fl_vfs_read,
    fl_vfs_wait,
};
/// The consumer VFS ABI: the opaque FlVfsMount, the result PODs (VfsData / VfsFileList /
/// VfsMountInfo / VfsMountResult), the VfsMountStatus / VfsLoadPriority /
/// VfsMountErrorStatus enums, and a read's VfsReadCallback post-processing hook and
/// VfsReleaseCallback user_data release hook.
pub use vfs_abi::vfs::{
    FlVfsMount, VfsData, VfsFileList, VfsLoadPriority, VfsMountErrorStatus, VfsMountInfo,
    VfsMountResult, VfsMountStatus, VfsReadCallback, VfsReleaseCallback,
};
pub use vfs_abi::vfs_listing::VfsListingCallback;
/// The VFS driver-plugin ABI: the VfsPlugin fn-pointer vtable a driver implements plus its
/// result types (VfsOpenResult / VfsReadResult / VfsSizeResult /
/// PluginCreateResult), the shared VfsDriverError, the VfsEntry listing record, and the
/// VfsStatus / VfsDriverCapabilities / VfsOpenFlags enums.
pub use vfs_abi::vfs_plugin::{
    PluginCreateResult, VfsDriverCapabilities, VfsDriverError, VfsEntry, VfsMountOptions,
    VfsOpenFlags, VfsOpenResult, VfsPlugin, VfsReadResult, VfsSizeResult, VfsStatus,
};

// Hand-declared entry points, exported by the same library as everything above.
extern "C" {
    /// Bring the VFS singleton up against arena and register the built-in
    /// LocalFS driver. False when it is already up, or when the job
    /// system has more workers than the VFS supports.
    pub fn fl_vfs_init(arena: *mut Arena) -> bool;

    /// The job the mount's own bring-up runs as, so work that reads through the
    /// mount can be scheduled after it. [`JobHandle::INVALID`] once the mount is up.
    pub fn fl_vfs_mount_get_job_handle(mount: *mut FlVfsMount) -> JobHandle;

    /// Register every archive driver this build of the library contains, after
    /// fl_vfs_init. A no-op when it contains none.
    pub fn fl_vfs_register_builtin_drivers();
}


mod ergonomics;

pub mod nav;

/// The 3D viewport seam: `MeshHandle`, `MeshVertex` and the `fl_mesh_*` / `fl_viewport3d_*` functions.
pub use toolkit::viewport3d;
pub use toolkit::viewport3d::*;

pub use toolkit::pixel_readback::PixelReadback;

pub use toolkit::application_settings::{AppRenderer, AppSettings};
/// The display-backend ABI: the `DisplayBackend` function-pointer table plus its envelopes.
pub use toolkit::backend_display::{
    DisplayBackend, DisplayConfig, DisplayFrameCaptureCallback, DisplayFramebuffer, DisplayRect,
};
/// The input-backend ABI: the `InputBackend` function-pointer table.
pub use toolkit::backend_input::InputBackend;
/// The `fl_background_*` functions.
pub use toolkit::background::{
    fl_background_set, fl_background_set_alpha, fl_background_set_blend,
    fl_background_set_clear_color,
};
pub use toolkit::button::*;
pub use toolkit::checkbox::*;
pub use toolkit::chip::*;
pub use toolkit::dropdown::*;
pub use toolkit::dynamic_image::*;
pub use toolkit::link::*;
pub use toolkit::pill::*;
pub use toolkit::progress_bar::*;
pub use toolkit::radio_group::*;
pub use toolkit::scrollbar::*;
pub use toolkit::spacer::*;
pub use toolkit::switch::*;
pub use toolkit::tab_bar::*;

/// The cursor domain: `CursorShape`, the opaque `Cursor` handle and the `fl_cursor_*` functions.
pub use toolkit::cursor::*;
/// The resolved within-frame element id (0 = nothing hot).
pub use toolkit::element_id::ElementId;
/// The by-value per-element interaction-and-geometry record; the rect is in physical pixels.
pub use toolkit::element_state::ElementState;
/// The `FloatingConfig` struct and its three enums.
pub use toolkit::floating::{
    FloatingAttachPoint, FloatingAttachTo, FloatingConfig, FloatingPointerCapture,
};
pub use toolkit::font::*;
/// The opaque font handle (0 = invalid / "active font").
pub use toolkit::font_id::FontId;
/// Font weight/style selector.
pub use toolkit::font_style::FontStyle;
/// The `Icon` enum and the `fl_icon_*` functions.
pub use toolkit::icon::{fl_icon_get, fl_icon_load, Icon};
pub use toolkit::image::*;
/// The opaque image handle (`ImageHandle::INVALID` = none).
pub use toolkit::image_handle::ImageHandle;
/// The `fl_input_*` event API: the event structs and their union payloads, the discriminant
/// enums, and the functions a host calls to push OS events into the UI context.
pub use toolkit::input::{
    GamepadAxis, GamepadAxisData, GamepadButton, GamepadButtonData, GamepadEvent,
    GamepadEventPayload, GamepadEventType, KeyEvent, KeyEventType, MouseButton, MouseButtonData,
    MouseEvent, MouseEventPayload, MouseEventType, MouseMoveData, MouseRelativeData,
    MouseWheelData, TextInputEvent, WindowDropData, WindowEvent, WindowEventPayload,
    WindowEventType, WindowResizeData,
};
pub use toolkit::input_device::InputDevice;
pub use toolkit::item_behavior::*;
/// The per-element behavior opt-in mask taken by `fl_item_behavior`.
pub use toolkit::item_flags::ItemFlags;
/// The keyboard-key discriminant.
pub use toolkit::key::Key;

impl Key {
    /// The `Key` a raw discriminant names, or `None` when it falls in the gap between the
    /// printable block (`Unknown`..=`Grave_Accent`) and the named block (`Escape`..=`Menu`), or
    /// past the end of the enum.
    #[inline]
    pub fn from_raw(raw: u32) -> Option<Key> {
        let named = raw <= Key::Grave_Accent as u32
            || (raw >= Key::Escape as u32 && raw <= Key::Menu as u32);
        // SAFETY: Key is a `#[repr(C)]` enum whose discriminants are exactly the two blocks tested above,
        // pinned by the assertion below, so raw is a value the enum actually has.
        named.then(|| unsafe { core::mem::transmute::<u32, Key>(raw) })
    }
}

const _: () = {
    assert!(
        Key::Grave_Accent as u32 == 48,
        "Key printable-block end drifted; from_raw must follow"
    );
    assert!(
        Key::Escape as u32 == 256,
        "Key named-block start drifted; from_raw must follow"
    );
    assert!(
        Key::Menu as u32 == 324,
        "Key named-block end drifted; from_raw must follow"
    );
    assert!(
        Key::Count as u32 == 325,
        "Key sentinel drifted; from_raw must follow"
    );
};

impl CursorShape {
    /// The shape a raw discriminant names, or `None` past the last shape.
    pub fn from_raw(raw: i32) -> Option<CursorShape> {
        Some(match raw {
            0 => CursorShape::Arrow,
            1 => CursorShape::Hand,
            2 => CursorShape::ResizeH,
            3 => CursorShape::ResizeV,
            4 => CursorShape::ResizeDiagonal1,
            5 => CursorShape::ResizeDiagonal2,
            6 => CursorShape::Text,
            7 => CursorShape::Crosshair,
            8 => CursorShape::NotAllowed,
            _ => return None,
        })
    }
}

const _: () = assert!(
    CursorShape::Count as i32 == 9,
    "CursorShape gained a variant; from_raw must follow"
);

/// The keyboard-modifier bitmask.
pub use toolkit::key_modifier::KeyModifier;
/// The `LayoutConfig` tree: the config struct, its nested structs, the enums and `LayoutId`.
pub use toolkit::layout::{
    BorderWidthConfig, CornerRadius, LayoutAlignmentX, LayoutAlignmentY, LayoutBorder,
    LayoutChildAlignment, LayoutClip, LayoutConfig, LayoutDirection, LayoutId, LayoutPadding,
    LayoutPlaced, LayoutSizing, LayoutSizingAxis, LayoutSizingAxisSize, LayoutSizingMinMax,
    LayoutSizingType,
};

/// The semantic action vocabulary (see [`nav`]).
pub use toolkit::action::Action;
pub use toolkit::focused::fl_focused_id;
/// The frame domain: alpha, delta time, screen size and frame-arena string reservation.
pub use toolkit::frame::*;
pub use toolkit::list::{
    DropRelationship, ListConfig, ListItem, ListOpts, ListRange, ListResult,
};
pub use toolkit::list_clipper::{fl_list_clipper_create, ListClipper};
pub use toolkit::list_uniform::{
    fl_list_uniform_begin, fl_list_uniform_end, fl_list_uniform_item_begin,
    fl_list_uniform_item_end, ListUniformOpts, ListUniformRange,
};
pub use toolkit::localization::*;
/// The context-menu config types.
pub use toolkit::menu::{
    MenuBarConfig, MenuBarMenuConfig, MenuClosePolicy, MenuConfig, MenuItemConfig,
    MenuRegionConfig, SubmenuConfig,
};
pub use toolkit::modal::*;
/// The navigation-edge bitmask a UI plugin's update returns.
pub use toolkit::nav_edge::NavEdge;
pub use toolkit::painter::*;
pub use toolkit::popup::{fl_popup_begin, fl_popup_end, fl_popup_message_box, fl_popup_tooltip};
/// The popup, dialog, tooltip and message-box config types.
pub use toolkit::popup::{
    DialogResult, MessageBoxConfig, PopupAnchor, PopupConfig, TooltipConfig,
};
/// `ScrollAreaConfig` and the `ScrollbarDisplayMode` its `scrollbar_mode` takes.
pub use toolkit::scroll::ScrollAreaConfig;
pub use toolkit::scrollbar_display::ScrollbarDisplayMode;
pub use toolkit::selection::{SelectionRequest, SelectionRequestKind};
/// The by-value widget interaction result.
pub use toolkit::signal::Signal;
pub use toolkit::slider::*;
/// The dash pattern of a border or stroked shape.
pub use toolkit::stroke_style::StrokeStyle;
pub use toolkit::text::*;
pub use toolkit::text_config::{Overflow, TextConfig, TextVAlign};
pub use nav::{
    fl_action_pressed, fl_consume_nav_down, fl_consume_nav_left, fl_consume_nav_right,
    fl_consume_nav_up, fl_get_nav_frame, fl_nav_down_pressed, fl_nav_up_pressed, fl_set_focus,
    fl_set_focus_id, NavFrame,
};

/// The design-token scales: spacing, corner-radius, control-height and stroke-weight steps.
pub use toolkit::design_tokens::{BorderWidth, Radius, Spacing, WidgetHeight};
pub use toolkit::text_input::*;
pub use toolkit::text_input_state;
/// The opaque text-input state; it crosses the ABI behind a `void*`.
pub use toolkit::text_input_state::TextInputState;
pub use toolkit::theme::{fl_theme_colors, fl_theme_pop, fl_theme_push, fl_theme_update};
/// The theme color tree.
pub use toolkit::theme::{
    ThemeBorder, ThemeColors, ThemeFeedback, ThemeInteractive, ThemeOverride, ThemeSlot,
    ThemeSurface, ThemeText,
};
pub use toolkit::ui_floating::*;
pub use toolkit::ui_input::*;
pub use toolkit::ui_layout::*;
pub use toolkit::ui_list::*;
pub use toolkit::ui_menu::*;
pub use toolkit::ui_scroll::*;
pub use toolkit::ui_selection::*;
/// The widget config tree: the shared `WidgetConfig` every widget embeds, its enums and the
/// per-widget config structs.
pub use toolkit::widget_config::{
    CheckboxConfig, DropdownConfig, Focusable, LinkConfig, ProgressConfig, RadioConfig,
    ScrollbarConfig, ScrollbarOrientation, SliderConfig, SwitchConfig, TabBarConfig,
    TextInputConfig, WidgetConfig, WidgetSize, WidgetVariant,
};

impl Default for WidgetConfig {
    /// All-zero: default variant and size, no overrides, null theme override, `Focusable::Default`.
    fn default() -> Self {
        // SAFETY: every scalar field of the all-zero config is 0/false, the only pointer field is
        // null, and every enum field has a 0 discriminant, so the bit pattern is valid.
        unsafe { core::mem::zeroed() }
    }
}

impl Default for DropdownConfig {
    /// The `ui_dropdown` macro's defaults: cancel row shown and a 200 px minimum width; every
    /// other field zero.
    fn default() -> Self {
        // SAFETY: as WidgetConfig above - all-zero is a valid bit pattern for this config.
        let zero: DropdownConfig = unsafe { core::mem::zeroed() };
        DropdownConfig {
            show_cancel: true,
            min_width: 200.0,
            ..zero
        }
    }
}

impl Default for TabBarConfig {
    fn default() -> Self {
        // SAFETY: as WidgetConfig above - all-zero is a valid bit pattern for this config.
        unsafe { core::mem::zeroed() }
    }
}

impl Default for RadioConfig {
    fn default() -> Self {
        // SAFETY: as WidgetConfig above - all-zero is a valid bit pattern for this config.
        unsafe { core::mem::zeroed() }
    }
}

pub use toolkit::app::*;
pub use toolkit::dock::*;

/// The embedded host tier: the opaque `Host` handle, its settings and the `fl_host_*` functions.
pub use toolkit::host::{
    fl_host_create, fl_host_destroy, fl_host_frame_update, fl_host_read_pixels, Host, HostSettings,
};

/// "Let the backend place the window" value for [`WindowSettings::x`] and `y`.
pub const FL_APP_WINDOW_POS_DEFAULT: c_int = i32::MIN;

/// A CPU's role on a heterogeneous SoC.
#[repr(C)]
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub enum CoreType {
    Unknown = 0,
    Performance,
    Efficiency,
}

/// An arena-allocated array of OS CPU ids and its length.
#[repr(C)]
#[derive(Copy, Clone)]
pub struct CpuIdList {
    pub ids: *mut c_int,
    pub count: c_int,
}

extern "C" {
    /// The OS CPU ids of every core of `core_type`, allocated from `arena`. On platforms
    /// without topology detection every core reports as [`CoreType::Unknown`], so a
    /// performance query is empty.
    pub fn fl_cpu_query_by_type(core_type: CoreType, arena: *mut Arena) -> CpuIdList;

    /// Pin `thread` (null for the calling thread) to `cpus`. True only when the pinning was
    /// applied; false on error, on a cpu id the platform does not have, and on platforms with
    /// no affinity support.
    pub fn fl_cpu_set_thread_affinity(thread: *mut c_void, cpus: CpuIdList) -> bool;
}

/// The running state of an incremental SHA256, held by value across [`fl_hash_sha256_update`] calls.
#[repr(C)]
#[derive(Copy, Clone)]
pub struct Sha256Context {
    pub state: [u32; 8],
    pub bitcount: u64,
    pub data: [u8; 64],
    pub datalen: u32,
}

const _: () = {
    assert!(
        core::mem::size_of::<Sha256Context>() == 112,
        "Sha256Context size drift"
    );
    assert!(
        core::mem::offset_of!(Sha256Context, bitcount) == 32,
        "Sha256Context.bitcount offset drift"
    );
    assert!(
        core::mem::offset_of!(Sha256Context, data) == 40,
        "Sha256Context.data offset drift"
    );
    assert!(
        core::mem::offset_of!(Sha256Context, datalen) == 104,
        "Sha256Context.datalen offset drift"
    );
};

extern "C" {
    /// CRC32 of a buffer (zlib's polynomial).
    pub fn fl_hash_crc32(data: *const u8, size: u64) -> u32;

    /// SHA1 of a buffer into a 20-byte digest.
    pub fn fl_hash_sha1(data: *const u8, size: u64, out: *mut u8);

    /// MD5 of a buffer into a 16-byte digest.
    pub fn fl_hash_md5(data: *const u8, size: u64, out: *mut u8);

    /// Start an incremental SHA256.
    pub fn fl_hash_sha256_init(ctx: *mut Sha256Context);

    /// Feed size bytes at data into an incremental SHA256.
    pub fn fl_hash_sha256_update(ctx: *mut Sha256Context, data: *const u8, size: u64);

    /// Finish an incremental SHA256 into a 32-byte digest.
    pub fn fl_hash_sha256_final(ctx: *mut Sha256Context, out: *mut u8);

    /// SHA256 of a buffer as a 64-char hex string plus NUL. `out`
    /// must have room for 65 bytes.
    pub fn fl_hash_sha256_hex(data: *const u8, size: u64, out: *mut c_char);

    /// Render a 32-byte SHA256 digest as a 64-char hex string plus NUL
    /// (`out` must have room for 65 bytes).
    pub fn fl_hash_sha256_digest_hex(digest: *const u8, out: *mut c_char);
}

#[cfg(test)]
mod tests {
    use super::{CursorShape, DropdownConfig};

    #[test]
    fn dropdown_default_matches_the_c_macro_defaults() {
        let config = DropdownConfig::default();
        assert!(config.show_cancel);
        assert_eq!(config.min_width, 200.0);
        assert!(!config.searchable);
        assert!(config.search_state.is_null());
    }

    #[test]
    fn cursor_shape_from_raw_accepts_only_real_shapes() {
        assert_eq!(CursorShape::from_raw(0), Some(CursorShape::Arrow));
        assert_eq!(CursorShape::from_raw(8), Some(CursorShape::NotAllowed));
        assert_eq!(CursorShape::from_raw(CursorShape::Count as i32), None);
        assert_eq!(CursorShape::from_raw(-1), None);
    }
}

#[cfg(test)]
mod ffi_guard_tests;
#[cfg(test)]
mod fl_error_tests;
#[cfg(test)]
mod flstring_abi_tests;
#[cfg(test)]
mod rawstr_tests;
