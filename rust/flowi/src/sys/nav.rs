//! Navigation and action queries for controller/keyboard-driven callers:
//! this frame's action presses, pending directional nav, and focus.

/// Snapshot of this frame's navigation inputs: who holds focus, plus which
/// directional nav is pending. Used for cross-frame tracking and
/// navigation-boundary auto-detection.
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq, Default)]
pub struct NavFrame {
    /// The element id holding focus this frame (0 = nothing focused).
    pub focused_id: u32,
    /// Nav-left is pending this frame.
    pub nav_left: bool,
    /// Nav-right is pending this frame.
    pub nav_right: bool,
    /// Nav-up is pending this frame.
    pub nav_up: bool,
    /// Nav-down is pending this frame.
    pub nav_down: bool,
}

// Layout matches the C FlNavFrame.
const _: () = {
    assert!(core::mem::size_of::<NavFrame>() == 8, "NavFrame size drift");
    assert!(
        core::mem::align_of::<NavFrame>() == 4,
        "NavFrame align drift"
    );
    assert!(
        core::mem::offset_of!(NavFrame, focused_id) == 0,
        "NavFrame.focused_id offset drift"
    );
    assert!(
        core::mem::offset_of!(NavFrame, nav_left) == 4,
        "NavFrame.nav_left offset drift"
    );
    assert!(
        core::mem::offset_of!(NavFrame, nav_right) == 5,
        "NavFrame.nav_right offset drift"
    );
    assert!(
        core::mem::offset_of!(NavFrame, nav_up) == 6,
        "NavFrame.nav_up offset drift"
    );
    assert!(
        core::mem::offset_of!(NavFrame, nav_down) == 7,
        "NavFrame.nav_down offset drift"
    );
};

extern "C" {
    /// True if action was pressed this frame.
    pub fn fl_action_pressed(action: super::Action) -> bool;

    /// True if navigation up was pressed this frame (arrow, D-pad, or left stick).
    pub fn fl_nav_up_pressed() -> bool;

    /// True if navigation down was pressed this frame (arrow, D-pad, or left stick).
    pub fn fl_nav_down_pressed() -> bool;

    /// Consume a pending nav-up so the focus system won't also act on it this frame.
    pub fn fl_consume_nav_up();

    /// Consume a pending nav-down so the focus system won't also act on it this frame.
    pub fn fl_consume_nav_down();

    /// Consume a pending nav-left so the focus system won't also act on it this frame.
    pub fn fl_consume_nav_left();

    /// Consume a pending nav-right so the focus system won't also act on it this frame.
    pub fn fl_consume_nav_right();

    /// Programmatically move focus to the widget identified by id.
    pub fn fl_set_focus(id: super::LayoutId);

    /// Programmatically move focus to an already-hashed element id.
    pub fn fl_set_focus_id(id: super::ElementId);

    /// Read this frame's navigation snapshot.
    pub fn fl_get_nav_frame() -> NavFrame;
}
