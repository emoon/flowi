//! The flowi layout DSL: function-like proc-macros that expand to the generated
//! begin/configure/body/end container calls, hiding the config struct.
//!
//! These macros are the primary layout API; the raw Layout::start /
//! Layout::configure / Layout::end trio covers what they do not take
//! (Element-anchored floating, which needs a hashed parent_id).
//!
//! The container macros share one surface: [`layout`] (general, explicit direction),
//! [`row`] / [`col`] (direction presets), [`scroll`] (a scroll area) and [`floating`]
//! (a floating element). Each takes an optional positional id, flat name: value fields
//! in any order, and a trailing { } body block. The id is any flowi::LayoutId: a bare
//! string, or a (name, seed) tuple to disambiguate repeated ids in a loop. A stable
//! file:line:column id is synthesised when it is omitted. Sizing for the width / height
//! fields comes from the sub-macros [`fixed`] / [`grow`] / [`percent`] / [`fit`],
//! which expand to a LayoutSizingAxis value. The remaining LayoutConfig fields have
//! matching sub-macros: [`clip`] (clipping + manual scroll), [`border`] (color +
//! uniform width), and [`align`] (child alignment).
//!
//! The macros are used via the flowi re-exports (flowi::col! { ... }) and need
//! nothing else in scope.

#![forbid(unsafe_code)]

mod align;
mod clip;
mod container;
mod emit;
mod fields;
mod layout;
mod menu;
mod sizing;

use proc_macro::TokenStream;

use crate::align::AlignArgs;
use crate::clip::ClipArgs;
use crate::container::Container;
use crate::emit::expand;
use crate::layout::LayoutKind;
use crate::menu::{MenuCheck, MenuRadio, MenuRegion, OpenConditional};
use crate::sizing::{MinMaxAxis, SizingArgs};

/// General layout container with an explicit direction field (defaults to a row).
#[proc_macro]
pub fn layout(input: TokenStream) -> TokenStream {
    expand(input, |c: Container| c.expand_layout(LayoutKind::Layout))
}

/// A row: a layout container with direction = LeftToRight.
#[proc_macro]
pub fn row(input: TokenStream) -> TokenStream {
    expand(input, |c: Container| c.expand_layout(LayoutKind::Row))
}

/// A column: a layout container with direction = TopToBottom.
#[proc_macro]
pub fn col(input: TokenStream) -> TokenStream {
    expand(input, |c: Container| c.expand_layout(LayoutKind::Col))
}

/// A placed child: a layout container positioned at an explicit offset (`[x, y]`) in its
/// parent's content coordinate space. It scrolls and clips with the parent, but does not advance
/// the flex cursor or grow the parent's content extent, so flowed siblings lay out as if it were
/// absent. Use it for coordinate-driven content (timeline clips, piano-roll notes, automation
/// points) inside a scroll/clip viewport. Accepts the same fields as [`layout`] (including
/// direction) plus offset (defaults to `[0, 0]`). The offset is in already-zoomed reference
/// pixels: the app bakes content zoom into the coordinate.
#[proc_macro]
pub fn placed(input: TokenStream) -> TokenStream {
    expand(input, |c: Container| c.expand_layout(LayoutKind::Placed))
}

/// A floating element (modal dialog, popup, tooltip). Closed with the ordinary
/// layout container end.
///
/// Fields: width / height (sizing), offset (`[x, y]`), z_index, attach
/// (a FloatingAttachTo), pointer_capture (a FloatingPointerCapture),
/// and element_attach / parent_attach (each a FloatingAttachPoint). When
/// attach is omitted it defaults to FloatingAttachTo::Root (absolute
/// positioning). pointer_capture defaults to Capture and both attach points
/// to LeftTop.
///
/// parent_id (for attach: FloatingAttachTo::Element) is not a field; use the raw
/// Floating::start / Floating::configure / Layout::end calls to set it.
#[proc_macro]
pub fn floating(input: TokenStream) -> TokenStream {
    expand(input, |c: Container| c.expand_floating())
}

/// A scroll area: a clipping container with managed scroll state.
#[proc_macro]
pub fn scroll(input: TokenStream) -> TokenStream {
    expand(input, |c: Container| c.expand_scroll())
}

/// A context menu (the Menu::begin/body/Menu::end pair). Like [`floating`] it takes an
/// optional positional id and flat fields, but its body runs only while the menu is open: it
/// expands to if Menu::begin(..) { body }, and a guard calls Menu::end() on every exit path.
/// The id is the menu chain's stable identity (share it with the
/// [`menu_region`] that opens it). Fields: min_width (logical px; 0.0 = default 180) and
/// z_index (0 = default 1000).
#[proc_macro]
pub fn menu(input: TokenStream) -> TokenStream {
    expand(input, |c: Container| c.expand_menu())
}

/// Emit one menu action item, expanding to [`menu_item_with`](../flowi/fn.menu_item_with.html) and returning
/// its Signal (so if menu_item!("Cut").clicked { ... } reads inline). Takes a required positional label and
/// flat fields: shortcut (&str, "" = none), close_policy (a MenuClosePolicy), and disabled (bool).
/// Unlike the container macros it takes no body block.
#[proc_macro]
pub fn menu_item(input: TokenStream) -> TokenStream {
    expand(input, |c: Container| c.expand_menu_item())
}

/// A right-click region (the Menu::region_begin/body/Menu::region_end pair): a right-click inside opens
/// menu_id at the cursor. Takes two positional ids (region_id, menu_id) and a trailing { body } of the
/// target content. Always runs its body (the region is always present), like [`scroll`]; the menu it opens is the
/// one a [`menu`] with the same menu_id renders. The region's Signal is discarded here; call
/// [`Menu::region_begin`](../flowi/struct.Menu.html#method.region_begin) directly if you need it.
#[proc_macro]
pub fn menu_region(input: TokenStream) -> TokenStream {
    expand(input, |r: MenuRegion| r.expand())
}

/// A checkable menu item ([`menu_check_with`](../flowi/fn.menu_check_with.html)) bound to a &mut bool,
/// returning its Signal (so if menu_check!("Bold", &mut bold).clicked { ... } reads inline). Takes a
/// required positional label and a &mut bound value, then the same flat fields as [`menu_item`]: shortcut
/// (&str), close_policy (a MenuClosePolicy), and disabled (bool). Takes no body block.
#[proc_macro]
pub fn menu_check(input: TokenStream) -> TokenStream {
    expand(input, |m: MenuCheck| m.expand())
}

/// A radio menu item ([`menu_radio_with`](../flowi/fn.menu_radio_with.html)) bound to a &mut i32 group state,
/// returning its Signal (so if menu_radio!("Sine", &mut wave, 0).clicked { ... } reads inline). Takes a
/// required positional label, a &mut bound value, and the option value, then the same flat fields as
/// [`menu_check`]. Takes no body block.
#[proc_macro]
pub fn menu_radio(input: TokenStream) -> TokenStream {
    expand(input, |m: MenuRadio| m.expand())
}

/// A non-interactive separator rule ([`Menu::separator`](../flowi/struct.Menu.html#method.separator)). Takes no
/// arguments, fields, or body.
#[proc_macro]
pub fn menu_separator(input: TokenStream) -> TokenStream {
    expand(input, |c: Container| c.expand_menu_separator())
}

/// A non-interactive section header ([`Menu::header`](../flowi/struct.Menu.html#method.header)). Takes a required
/// positional label and nothing else.
#[proc_macro]
pub fn menu_header(input: TokenStream) -> TokenStream {
    expand(input, |c: Container| c.expand_menu_header())
}

/// A submenu (the UiSubmenu::begin/body/UiSubmenu::end pair). Like [`menu`] its body runs only while
/// the submenu is expanded: it expands to if UiSubmenu::begin(..) { body }, and a guard calls UiSubmenu::end()
/// on every exit path. Takes a required positional label (the parent row's text and identity), the flat fields
/// min_width (logical px; 0.0 = default 180) and disabled (bool), and the trailing { body } of child items.
#[proc_macro]
pub fn submenu(input: TokenStream) -> TokenStream {
    expand(input, |c: Container| c.expand_open_conditional(OpenConditional::submenu()))
}

/// The menu bar (the MenuBar::begin/body/MenuBar::end pair). Like [`scroll`], and unlike [`menu`], its
/// body always runs: the strip is the persistent File/Edit/View surface, so it expands to
/// MenuBar::begin(..); body; MenuBar::end(); with the tops ([`menubar_menu!`]) emitted in the body every frame.
/// Takes an optional positional id (the bar's stable identity), a single flat field z_index (0 =
/// default 1000), and the trailing { body } of tops.
#[proc_macro]
pub fn menubar(input: TokenStream) -> TokenStream {
    expand(input, |c: Container| c.expand_menubar())
}

/// One top in the menu bar (the UiMenubarMenu::begin/body/UiMenubarMenu::end pair). Like [`submenu`] its body
/// runs only while the dropdown is open (a click on the top opens it): it expands to
/// if UiMenubarMenu::begin(..) { body }, and a guard calls UiMenubarMenu::end() on every exit path. Takes a
/// required positional label (the top's text and identity), the flat fields min_width (logical px; 0.0 =
/// default 180) and disabled (bool), and the trailing { body } of dropdown items.
#[proc_macro]
pub fn menubar_menu(input: TokenStream) -> TokenStream {
    expand(input, |c: Container| c.expand_open_conditional(OpenConditional::menubar_menu()))
}

/// A fixed axis: min == max == value. Exactly one argument.
#[proc_macro]
pub fn fixed(input: TokenStream) -> TokenStream {
    expand(input, |a: SizingArgs| a.expand_fixed())
}

/// A grow axis (grow!(), grow!(min), or grow!(min, max)).
#[proc_macro]
pub fn grow(input: TokenStream) -> TokenStream {
    expand(input, |a: SizingArgs| a.expand_min_max(MinMaxAxis::Grow))
}

/// A fit axis (fit!(), fit!(min), or fit!(min, max)).
#[proc_macro]
pub fn fit(input: TokenStream) -> TokenStream {
    expand(input, |a: SizingArgs| a.expand_min_max(MinMaxAxis::Fit))
}

/// A percent axis: a fraction of the parent's size. Exactly one argument (0.0..=1.0).
#[proc_macro]
pub fn percent(input: TokenStream) -> TokenStream {
    expand(input, |a: SizingArgs| a.expand_percent())
}

/// The macro form of the clip field: a LayoutClip from bare-ident flags and an optional
/// scroll_pos: value. `clip!(horizontal, manual_scroll, scroll_pos: [x, 0.0])`.
#[proc_macro]
pub fn clip(input: TokenStream) -> TokenStream {
    expand(input, |a: ClipArgs| a.expand())
}

/// The macro form of the border field: a LayoutBorder from a positional color and a
/// uniform width. between_children defaults to 0 and the stroke to solid.
#[proc_macro]
pub fn border(input: TokenStream) -> TokenStream {
    expand(input, |a: SizingArgs| a.expand_border())
}

/// The macro form of the align field: a LayoutChildAlignment from positional bare-ident
/// variants. align!(Center, Top): first arg is the x variant, second the y.
#[proc_macro]
pub fn align(input: TokenStream) -> TokenStream {
    expand(input, |a: AlignArgs| a.expand())
}

#[cfg(test)]
mod tests {
    use super::*;
    use quote::quote;
    use syn::Expr;

    use crate::fields::Fields;

    #[test]
    fn fallback_id_is_file_line_column() {
        let c = Container {
            id: None,
            fields: Fields::default(),
            body: None,
        };
        let expected = quote! {
            ::core::concat!(::core::file!(), ":", ::core::line!(), ":", ::core::column!())
        };
        assert_eq!(c.id_tokens().to_string(), expected.to_string());
    }

    #[test]
    fn explicit_id_passes_through() {
        let e: Expr = syn::parse_str("\"root\"").unwrap();
        let c = Container {
            id: Some(e),
            fields: Fields::default(),
            body: None,
        };
        assert_eq!(c.id_tokens().to_string(), quote! { "root" }.to_string());
    }
}
