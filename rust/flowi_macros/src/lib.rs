//! The flowi layout DSL: function-like proc-macros that expand to the generated
//! begin/configure/body/end container calls, hiding the config struct.
//!
//! These macros are the primary layout API; the raw Layout::start /
//! Layout::configure / Layout::end trio covers what they do not take
//! (Element-anchored floating, which needs a hashed parent_id).
//!
//! Container macros - [`layout`] (general, explicit direction), [`row`] / [`col`]
//! (direction presets), [`scroll`] (a scroll area), [`floating`] (a floating
//! element) - share one surface: an optional positional id (any flowi::LayoutId -
//! a bare string, or a (name, seed) tuple to disambiguate repeated ids in a
//! loop; a stable file:line:column id is synthesised when omitted), flat name: value
//! fields in any order, and a trailing { } body block. Sizing for the width / height
//! fields comes from the sub-macros [`fixed`] / [`grow`] / [`percent`] / [`fit`],
//! which expand to a LayoutSizingAxis value; the remaining LayoutConfig
//! fields have matching sub-macros - [`clip`] (clipping + manual scroll),
//! [`border`] (color + uniform width), and [`align`] (child alignment).
//!
//! The macros are used via the flowi re-exports (flowi::col! { ... }) and need
//! nothing else in scope.

#![forbid(unsafe_code)]

use proc_macro::TokenStream;
use proc_macro2::TokenStream as TokenStream2;
use quote::quote;
use syn::parse::{Parse, ParseStream};
use syn::punctuated::Punctuated;
use syn::{braced, parse_macro_input, token, Expr, Ident, Token};

/// One parsed container invocation: an optional positional id, flat name: value
/// fields, and an optional trailing { body } block.
struct Container {
    id: Option<Expr>,
    fields: Vec<(Ident, Expr)>,
    body: Option<TokenStream2>,
}

impl Parse for Container {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        // Optional positional id, parsed without eager-brace so a trailing { ... }
        // body is never mistaken for a struct literal.
        let starts_with_field = input.peek(Ident) && input.peek2(Token![:]);
        let id = if input.is_empty() || input.peek(token::Brace) || starts_with_field {
            None
        } else {
            let e: Expr = input.call(Expr::parse_without_eager_brace)?;
            if input.peek(Token![,]) {
                input.parse::<Token![,]>()?;
            }
            Some(e)
        };

        let mut fields = Vec::new();
        while !input.is_empty() && !input.peek(token::Brace) {
            let key: Ident = input.parse()?;
            input.parse::<Token![:]>()?;
            let val: Expr = input.call(Expr::parse_without_eager_brace)?;
            fields.push((key, val));
            if input.peek(Token![,]) {
                input.parse::<Token![,]>()?;
            } else {
                break;
            }
        }

        let body = if input.peek(token::Brace) {
            let content;
            braced!(content in input);
            Some(content.parse::<TokenStream2>()?)
        } else {
            None
        };

        if !input.is_empty() {
            return Err(input.error("unexpected tokens after container body"));
        }

        Ok(Container { id, fields, body })
    }
}

/// First field value matching name (fields are validated unique by [`check_keys`]).
fn find<'a>(fields: &'a [(Ident, Expr)], name: &str) -> Option<&'a Expr> {
    fields.iter().find(|(k, _)| *k == name).map(|(_, v)| v)
}

fn check_keys(fields: &[(Ident, Expr)], allowed: &[&str]) -> syn::Result<()> {
    let mut seen: Vec<String> = Vec::new();
    for (k, _) in fields {
        let ks = k.to_string();
        if !allowed.contains(&ks.as_str()) {
            return Err(syn::Error::new(
                k.span(),
                format!("unknown field `{}`; allowed: {}", ks, allowed.join(", ")),
            ));
        }
        if seen.contains(&ks) {
            return Err(syn::Error::new(
                k.span(),
                format!("duplicate field `{}`", ks),
            ));
        }
        seen.push(ks);
    }
    Ok(())
}

/// Build the ::flowi::MenuItemConfig { ... } literal shared by the leaf item macros. The caller validates
/// the field names via [`check_keys`] first; an absent field is zeroed.
fn menu_item_config_tokens(fields: &[(Ident, Expr)]) -> TokenStream2 {
    let shortcut = find(fields, "shortcut")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { "" });
    let close_policy = find(fields, "close_policy")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { ::flowi::MenuClosePolicy::Default });
    let disabled = find(fields, "disabled")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { false });
    quote! {
        ::flowi::MenuItemConfig {
            close_policy: #close_policy,
            shortcut: #shortcut,
            disabled: #disabled,
        }
    }
}

/// Parse an optional trailing , name: value, ... field list (no body block). A trailing comma is
/// tolerated; any non-field remainder is an error.
fn parse_trailing_fields(input: ParseStream) -> syn::Result<Vec<(Ident, Expr)>> {
    let mut fields = Vec::new();
    while input.peek(Token![,]) {
        input.parse::<Token![,]>()?;
        if input.is_empty() {
            break;
        }
        let key: Ident = input.parse()?;
        input.parse::<Token![:]>()?;
        let val: Expr = input.call(Expr::parse_without_eager_brace)?;
        fields.push((key, val));
    }
    if !input.is_empty() {
        return Err(input.error("unexpected tokens after menu item fields"));
    }
    Ok(fields)
}

/// The container's positional id, or a compile error naming the macro when it is omitted. Err already
/// carries the compile_error! expansion, so callers just return it.
fn require_label(c: &Container, macro_name: &str) -> Result<TokenStream2, TokenStream> {
    match &c.id {
        Some(e) => Ok(quote! { #e }),
        None => Err(syn::Error::new(
            proc_macro2::Span::call_site(),
            format!("{macro_name}! requires a label"),
        )
        .to_compile_error()
        .into()),
    }
}

/// The container id: the positional expression, or a synthesised stable
/// file:line:column string when omitted. Both resolve to an flowi::LayoutId via
/// Into at the call site. The column keeps two id-less invocations on one source line distinct.
fn id_tokens(c: &Container) -> TokenStream2 {
    match &c.id {
        Some(e) => quote! { #e },
        None => {
            quote! { ::core::concat!(::core::file!(), ":", ::core::line!(), ":", ::core::column!()) }
        }
    }
}

/// The body block, defaulting to empty. Wrapped by the caller in its own { } so a
/// trailing expression is the block's (discarded) value rather than a stray statement.
fn body_tokens(c: &Container) -> TokenStream2 {
    c.body.clone().unwrap_or_default()
}

/// A field of the neutral `LayoutConfig`, the value an omitted macro field takes.
fn default_field(field: TokenStream2) -> TokenStream2 {
    quote! { ::flowi::LayoutConfig::default().#field }
}

fn zero_radius() -> TokenStream2 {
    default_field(quote! { radius })
}

fn zero_padding() -> TokenStream2 {
    default_field(quote! { padding })
}

fn transparent() -> TokenStream2 {
    default_field(quote! { color })
}

fn default_align() -> TokenStream2 {
    default_field(quote! { child_alignment })
}

fn default_clip() -> TokenStream2 {
    default_field(quote! { clip })
}

fn default_border() -> TokenStream2 {
    default_field(quote! { border })
}

fn default_gap() -> TokenStream2 {
    default_field(quote! { child_gap })
}

fn default_direction() -> TokenStream2 {
    default_field(quote! { direction })
}

fn default_fit() -> TokenStream2 {
    quote! { ::flowi::LayoutSizingAxis::FIT }
}

fn default_grow() -> TokenStream2 {
    quote! { ::flowi::LayoutSizingAxis::GROW }
}

/// Color from a `[r, g, b]` / `[r, g, b, a]` array literal (alpha defaults to
/// 255), or any other expression passed through as a full Color value.
fn color_tokens(expr: &Expr) -> syn::Result<TokenStream2> {
    if let Expr::Array(arr) = expr {
        let elems: Vec<&Expr> = arr.elems.iter().collect();
        match elems.len() {
            3 => {
                let (r, g, b) = (elems[0], elems[1], elems[2]);
                return Ok(quote! { ::flowi::Color { r: #r, g: #g, b: #b, a: 255 } });
            }
            4 => {
                let (r, g, b, a) = (elems[0], elems[1], elems[2], elems[3]);
                return Ok(quote! { ::flowi::Color { r: #r, g: #g, b: #b, a: #a } });
            }
            _ => {
                return Err(syn::Error::new(
                    arr.bracket_token.span.join(),
                    "color array must have 3 ([r, g, b]) or 4 ([r, g, b, a]) elements",
                ))
            }
        }
    }
    Ok(quote! { (#expr) })
}

/// Vec2 from an `[x, y]` array literal, or any other expression passed through.
fn vec2_tokens(expr: &Expr) -> syn::Result<TokenStream2> {
    if let Expr::Array(arr) = expr {
        let elems: Vec<&Expr> = arr.elems.iter().collect();
        if elems.len() == 2 {
            let (x, y) = (elems[0], elems[1]);
            return Ok(quote! { ::flowi::Vec2 { x: ((#x) as f32), y: ((#y) as f32) } });
        }
        return Err(syn::Error::new(
            arr.bracket_token.span.join(),
            "offset array must have 2 ([x, y]) elements",
        ));
    }
    Ok(quote! { (#expr) })
}

/// LayoutPadding from the padding: field: accepts a scalar u16 (uniform), a (vertical, horizontal)
/// pair, a `[top, right, bottom, left]` array, or a LayoutPadding (identity From). An unsuffixed literal
/// still infers because u16 is the only integer source impl.
fn padding_tokens(expr: &Expr) -> TokenStream2 {
    quote! {
        {
            let __pad: ::flowi::LayoutPadding = ::core::convert::Into::into(#expr);
            __pad
        }
    }
}

/// CornerRadius from the radius: field: accepts a scalar f32 (uniform, via `From<f32>`) or a
/// per-corner CornerRadius (identity From).
fn radius_tokens(expr: &Expr) -> TokenStream2 {
    quote! {
        {
            let __r: ::flowi::CornerRadius = ::core::convert::Into::into(#expr);
            __r
        }
    }
}

/// Build the LayoutConfig literal for [`layout`] / [`row`] / [`col`]. When
/// forced_dir is set (row/col), direction is not an accepted field.
fn layout_config(
    c: &Container,
    forced_dir: Option<&str>,
    placed: bool,
) -> syn::Result<TokenStream2> {
    const FIELDS: &[&str] = &[
        "width", "height", "padding", "gap", "color", "radius", "clip", "border", "align",
    ];
    const FIELDS_WITH_DIR: &[&str] = &[
        "width",
        "height",
        "padding",
        "gap",
        "color",
        "radius",
        "clip",
        "border",
        "align",
        "direction",
    ];
    const FIELDS_PLACED: &[&str] = &[
        "width",
        "height",
        "padding",
        "gap",
        "color",
        "radius",
        "clip",
        "border",
        "align",
        "direction",
        "offset",
    ];
    let allowed: &[&str] = if placed {
        FIELDS_PLACED
    } else if forced_dir.is_none() {
        FIELDS_WITH_DIR
    } else {
        FIELDS
    };
    check_keys(&c.fields, allowed)?;

    let width = find(&c.fields, "width")
        .map(|e| quote! { #e })
        .unwrap_or_else(default_fit);
    let height = find(&c.fields, "height")
        .map(|e| quote! { #e })
        .unwrap_or_else(default_fit);
    let padding = find(&c.fields, "padding")
        .map(padding_tokens)
        .unwrap_or_else(zero_padding);
    let gap = find(&c.fields, "gap")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(default_gap);
    let radius = find(&c.fields, "radius")
        .map(radius_tokens)
        .unwrap_or_else(zero_radius);
    let color = match find(&c.fields, "color") {
        Some(e) => color_tokens(e)?,
        None => transparent(),
    };
    let clip = find(&c.fields, "clip")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(default_clip);
    let border = find(&c.fields, "border")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(default_border);
    let child_alignment = find(&c.fields, "align")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(default_align);
    let direction = match forced_dir {
        Some("LeftToRight") => quote! { ::flowi::LayoutDirection::LeftToRight },
        Some("TopToBottom") => quote! { ::flowi::LayoutDirection::TopToBottom },
        Some(other) => unreachable!("unknown forced direction {other}"),
        None => find(&c.fields, "direction")
            .map(|e| quote! { (#e) })
            .unwrap_or_else(default_direction),
    };
    let placed = if placed {
        let offset = match find(&c.fields, "offset") {
            Some(e) => vec2_tokens(e)?,
            None => default_field(quote! { placed.offset }),
        };
        quote! { ::flowi::LayoutPlaced { offset: #offset, enabled: true } }
    } else {
        default_field(quote! { placed })
    };

    Ok(quote! {
        ::flowi::LayoutConfig {
            sizing: ::flowi::LayoutSizing { width: #width, height: #height },
            padding: #padding,
            child_gap: #gap,
            radius: #radius,
            child_alignment: #child_alignment,
            direction: #direction,
            color: #color,
            clip: #clip,
            border: #border,
            placed: #placed,
        }
    })
}

/// Build the FloatingConfig literal for [`floating`]. attach defaults to
/// Root so the element floats (absolute) without extra configuration.
fn floating_config(c: &Container) -> syn::Result<TokenStream2> {
    check_keys(
        &c.fields,
        &[
            "width",
            "height",
            "offset",
            "z_index",
            "attach",
            "pointer_capture",
            "element_attach",
            "parent_attach",
        ],
    )?;

    let width = find(&c.fields, "width")
        .map(|e| quote! { #e })
        .unwrap_or_else(default_fit);
    let height = find(&c.fields, "height")
        .map(|e| quote! { #e })
        .unwrap_or_else(default_fit);
    let offset = match find(&c.fields, "offset") {
        Some(e) => vec2_tokens(e)?,
        None => quote! { ::flowi::Vec2 { x: 0.0, y: 0.0 } },
    };
    let z_index = find(&c.fields, "z_index")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { 0i16 });
    let attach = find(&c.fields, "attach")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { ::flowi::FloatingAttachTo::Root });
    let pointer_capture = find(&c.fields, "pointer_capture")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { ::flowi::FloatingPointerCapture::Capture });
    let element_attach = find(&c.fields, "element_attach")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { ::flowi::FloatingAttachPoint::LeftTop });
    let parent_attach = find(&c.fields, "parent_attach")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { ::flowi::FloatingAttachPoint::LeftTop });

    Ok(quote! {
        ::flowi::FloatingConfig {
            attach_to: #attach,
            pointer_capture: #pointer_capture,
            z_index: #z_index,
            offset: #offset,
            element_attach: #element_attach,
            parent_attach: #parent_attach,
            parent_id: 0u32,
            sizing: ::flowi::LayoutSizing { width: #width, height: #height },
        }
    })
}

/// Build the ScrollAreaConfig literal for [`scroll`]. Sizing defaults to
/// Grow/Grow (the engine's scroll-area default).
fn scroll_config(c: &Container) -> syn::Result<TokenStream2> {
    check_keys(
        &c.fields,
        &[
            "width",
            "height",
            "gap",
            "padding",
            "color",
            "radius",
            "direction",
            "scrollbar",
            "scrollbar_mode",
            "smooth",
            "speed",
        ],
    )?;

    let width = find(&c.fields, "width")
        .map(|e| quote! { #e })
        .unwrap_or_else(default_grow);
    let height = find(&c.fields, "height")
        .map(|e| quote! { #e })
        .unwrap_or_else(default_grow);
    let direction = find(&c.fields, "direction")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { ::flowi::LayoutDirection::TopToBottom });
    let gap = find(&c.fields, "gap")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(default_gap);
    let padding = find(&c.fields, "padding")
        .map(padding_tokens)
        .unwrap_or_else(zero_padding);
    let color = match find(&c.fields, "color") {
        Some(e) => color_tokens(e)?,
        None => transparent(),
    };
    let radius = find(&c.fields, "radius")
        .map(radius_tokens)
        .unwrap_or_else(zero_radius);
    let scrollbar = find(&c.fields, "scrollbar")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { false });
    let scrollbar_mode = find(&c.fields, "scrollbar_mode")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { ::flowi::ScrollbarDisplayMode::Auto });
    let smooth = find(&c.fields, "smooth")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { true });
    let speed = find(&c.fields, "speed")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { 30.0f32 });

    Ok(quote! {
        ::flowi::ScrollAreaConfig {
            sizing: ::flowi::LayoutSizing { width: #width, height: #height },
            direction: #direction,
            child_gap: #gap,
            padding: #padding,
            background_color: #color,
            corner_radius: #radius,
            show_scrollbar: #scrollbar,
            scrollbar_mode: #scrollbar_mode,
            smooth_scroll: #smooth,
            scroll_speed: #speed,
        }
    })
}

/// Emit start; configure; guard; { body } for a layout container. The end
/// call lives in the guard's Drop, so the container closes even when the body
/// panics or returns early.
fn expand_layout(c: &Container, forced_dir: Option<&str>, placed: bool) -> TokenStream {
    let config = match layout_config(c, forced_dir, placed) {
        Ok(t) => t,
        Err(e) => return e.to_compile_error().into(),
    };
    let id = id_tokens(c);
    let body = body_tokens(c);
    quote! {
        {
            ::flowi::Layout::start(#id);
            ::flowi::Layout::configure(#config);
            let __fl_layout_end_guard = ::flowi::LayoutEndGuard::new();
            { #body }
        }
    }
    .into()
}

/// General layout container with an explicit direction field (defaults to a row).
#[proc_macro]
pub fn layout(input: TokenStream) -> TokenStream {
    let c = parse_macro_input!(input as Container);
    expand_layout(&c, None, false)
}

/// A row: a layout container with direction = LeftToRight.
#[proc_macro]
pub fn row(input: TokenStream) -> TokenStream {
    let c = parse_macro_input!(input as Container);
    expand_layout(&c, Some("LeftToRight"), false)
}

/// A column: a layout container with direction = TopToBottom.
#[proc_macro]
pub fn col(input: TokenStream) -> TokenStream {
    let c = parse_macro_input!(input as Container);
    expand_layout(&c, Some("TopToBottom"), false)
}

/// A placed child: a layout container positioned at an explicit offset (`[x, y]`) in its
/// parent's content coordinate space - it scrolls and clips with the parent, but does not advance
/// the flex cursor or grow the parent's content extent, so flowed siblings lay out as if it were
/// absent. Use it for coordinate-driven content (timeline clips, piano-roll notes, automation
/// points) inside a scroll/clip viewport. Accepts the same fields as [`layout`] (including
/// direction) plus offset (defaults to `[0, 0]`). The offset is in already-zoomed reference
/// pixels - the app bakes content zoom into the coordinate.
#[proc_macro]
pub fn placed(input: TokenStream) -> TokenStream {
    let c = parse_macro_input!(input as Container);
    expand_layout(&c, None, true)
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
    let c = parse_macro_input!(input as Container);
    let config = match floating_config(&c) {
        Ok(t) => t,
        Err(e) => return e.to_compile_error().into(),
    };
    let id = id_tokens(&c);
    let body = body_tokens(&c);
    quote! {
        {
            ::flowi::Floating::start(#id);
            ::flowi::Floating::configure(#config);
            let __fl_layout_end_guard = ::flowi::LayoutEndGuard::new();
            { #body }
        }
    }
    .into()
}

/// A scroll area: a clipping container with managed scroll state.
#[proc_macro]
pub fn scroll(input: TokenStream) -> TokenStream {
    let c = parse_macro_input!(input as Container);
    let config = match scroll_config(&c) {
        Ok(t) => t,
        Err(e) => return e.to_compile_error().into(),
    };
    let id = id_tokens(&c);
    let body = body_tokens(&c);
    quote! {
        {
            ::flowi::Scroll::area_begin(#id, #config);
            let __fl_scroll_end_guard = ::flowi::ScrollEndGuard::new();
            { #body }
        }
    }
    .into()
}

/// A context menu (the Menu::begin/body/Menu::end pair). Like [`floating`] it takes an
/// optional positional id and flat fields, but - unlike the always-run container macros - its
/// body runs only while the menu is open: it expands to an if Menu::begin(..) { body; Menu::end(); },
/// the begin-returns-bool conditional. The id is the menu chain's stable identity (share it with the
/// [`menu_region`] that opens it). Fields: min_width (logical px; 0.0 = default 180) and
/// z_index (0 = default 1000).
#[proc_macro]
pub fn menu(input: TokenStream) -> TokenStream {
    let c = parse_macro_input!(input as Container);
    if let Err(e) = check_keys(&c.fields, &["min_width", "z_index"]) {
        return e.to_compile_error().into();
    }
    let id = id_tokens(&c);
    let body = body_tokens(&c);
    let min_width = find(&c.fields, "min_width")
        .map(|e| quote! { ((#e) as f32) })
        .unwrap_or_else(|| quote! { 0.0f32 });
    let z_index = find(&c.fields, "z_index")
        .map(|e| quote! { ((#e) as i16) })
        .unwrap_or_else(|| quote! { 0i16 });
    quote! {
        if ::flowi::Menu::begin(#id, ::flowi::MenuConfig { min_width: #min_width, z_index: #z_index }) {
            let __fl_menu_end_guard = ::flowi::MenuEndGuard::new();
            { #body }
        }
    }
    .into()
}

/// Emit one menu action item, expanding to [`menu_item_with`](../flowi/fn.menu_item_with.html) and returning
/// its Signal (so if menu_item!("Cut").clicked { ... } reads inline). Takes a required positional label and
/// flat fields - shortcut (&str, "" = none), close_policy (a MenuClosePolicy), and disabled (bool).
/// Unlike the container macros it takes no body block.
#[proc_macro]
pub fn menu_item(input: TokenStream) -> TokenStream {
    let c = parse_macro_input!(input as Container);
    if c.body.is_some() {
        return syn::Error::new(
            proc_macro2::Span::call_site(),
            "menu_item! takes no body block",
        )
        .to_compile_error()
        .into();
    }
    let label = match require_label(&c, "menu_item") {
        Ok(l) => l,
        Err(e) => return e,
    };
    if let Err(e) = check_keys(&c.fields, &["shortcut", "close_policy", "disabled"]) {
        return e.to_compile_error().into();
    }
    let config = menu_item_config_tokens(&c.fields);
    quote! {
        ::flowi::menu_item_with(#label, #config)
    }
    .into()
}

/// A right-click region (the Menu::region_begin/body/Menu::region_end pair): a right-click inside opens
/// menu_id at the cursor. Takes two positional ids - region_id, menu_id - and a trailing { body } of the
/// target content. Always runs its body (the region is always present), like [`scroll`]; the menu it opens is the
/// one a [`menu`] with the same menu_id renders. The region's Signal is discarded here - call
/// [`Menu::region_begin`](../flowi/struct.Menu.html#method.region_begin) directly if you need it.
#[proc_macro]
pub fn menu_region(input: TokenStream) -> TokenStream {
    let r = parse_macro_input!(input as MenuRegion);
    let region_id = &r.region_id;
    let menu_id = &r.menu_id;
    let body = r.body.unwrap_or_default();
    quote! {
        {
            ::flowi::Menu::region_begin(#region_id, #menu_id);
            let __fl_menu_region_end_guard = ::flowi::MenuRegionEndGuard::new();
            { #body }
        }
    }
    .into()
}

/// A parsed [`menu_region`] invocation: two positional ids and an optional trailing body block.
struct MenuRegion {
    region_id: Expr,
    menu_id: Expr,
    body: Option<TokenStream2>,
}

impl Parse for MenuRegion {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        // Parsed without eager-brace so a trailing { body } is never mistaken for a struct literal.
        let region_id: Expr = input.call(Expr::parse_without_eager_brace)?;
        input.parse::<Token![,]>()?;
        let menu_id: Expr = input.call(Expr::parse_without_eager_brace)?;
        if input.peek(Token![,]) {
            input.parse::<Token![,]>()?;
        }
        let body = if input.peek(token::Brace) {
            let content;
            braced!(content in input);
            Some(content.parse::<TokenStream2>()?)
        } else {
            None
        };
        if !input.is_empty() {
            return Err(input.error("unexpected tokens after menu_region! body"));
        }
        Ok(MenuRegion {
            region_id,
            menu_id,
            body,
        })
    }
}

/// A checkable menu item ([`menu_check_with`](../flowi/fn.menu_check_with.html)) bound to a &mut bool,
/// returning its Signal (so if menu_check!("Bold", &mut bold).clicked { ... } reads inline). Takes a
/// required positional label and a &mut bound value, then flat fields - shortcut (&str), close_policy
/// (a MenuClosePolicy), and disabled (bool) - the same set as [`menu_item`]. Takes no body block.
#[proc_macro]
pub fn menu_check(input: TokenStream) -> TokenStream {
    let m = parse_macro_input!(input as MenuCheck);
    if let Err(e) = check_keys(&m.fields, &["shortcut", "close_policy", "disabled"]) {
        return e.to_compile_error().into();
    }
    let label = &m.label;
    let bound = &m.bound;
    let config = menu_item_config_tokens(&m.fields);
    quote! {
        ::flowi::menu_check_with(#label, #bound, #config)
    }
    .into()
}

/// A radio menu item ([`menu_radio_with`](../flowi/fn.menu_radio_with.html)) bound to a &mut i32 group state,
/// returning its Signal (so if menu_radio!("Sine", &mut wave, 0).clicked { ... } reads inline). Takes a
/// required positional label, a &mut bound value, and the option value, then the same flat fields as
/// [`menu_check`]. Takes no body block.
#[proc_macro]
pub fn menu_radio(input: TokenStream) -> TokenStream {
    let m = parse_macro_input!(input as MenuRadio);
    if let Err(e) = check_keys(&m.fields, &["shortcut", "close_policy", "disabled"]) {
        return e.to_compile_error().into();
    }
    let label = &m.label;
    let bound = &m.bound;
    let value = &m.value;
    let config = menu_item_config_tokens(&m.fields);
    quote! {
        ::flowi::menu_radio_with(#label, #bound, #value, #config)
    }
    .into()
}

/// A non-interactive separator rule ([`menu_separator`](../flowi/fn.menu_separator.html)). Takes no arguments,
/// fields, or body.
#[proc_macro]
pub fn menu_separator(input: TokenStream) -> TokenStream {
    let c = parse_macro_input!(input as Container);
    if c.id.is_some() || !c.fields.is_empty() || c.body.is_some() {
        return syn::Error::new(
            proc_macro2::Span::call_site(),
            "menu_separator! takes no arguments",
        )
        .to_compile_error()
        .into();
    }
    quote! { ::flowi::Menu::separator() }.into()
}

/// A non-interactive section header ([`menu_header`](../flowi/fn.menu_header.html)). Takes a required positional
/// label and nothing else.
#[proc_macro]
pub fn menu_header(input: TokenStream) -> TokenStream {
    let c = parse_macro_input!(input as Container);
    if !c.fields.is_empty() || c.body.is_some() {
        return syn::Error::new(
            proc_macro2::Span::call_site(),
            "menu_header! takes only a label",
        )
        .to_compile_error()
        .into();
    }
    let label = match require_label(&c, "menu_header") {
        Ok(l) => l,
        Err(e) => return e,
    };
    quote! { ::flowi::Menu::header(#label) }.into()
}

/// A submenu (the UiSubmenu::begin/body/UiSubmenu::end pair). Like [`menu`] its body runs only while
/// the submenu is expanded: it expands to an if UiSubmenu::begin(..) { body; UiSubmenu::end(); }. Takes
/// a required positional label (the parent row's text and identity) and flat fields - min_width (logical px;
/// 0.0 = default 180) and disabled (bool) - plus the trailing { body } of child items.
#[proc_macro]
pub fn submenu(input: TokenStream) -> TokenStream {
    expand_labeled_open_conditional(
        input,
        "submenu",
        quote! { ::flowi::UiSubmenu::begin },
        quote! { ::flowi::SubmenuConfig },
        quote! { ::flowi::SubmenuEndGuard },
    )
}

/// Shared expansion for the label-plus-min_width/disabled open-conditional menu macros ([`submenu`] /
/// [`menubar_menu`]): `if <begin>(label, <Config> { min_width, disabled }) { let _g = <guard>::new(); body }` - the body
/// running only while the nested level is open, closed by the guard on every exit path.
fn expand_labeled_open_conditional(
    input: TokenStream,
    macro_name: &str,
    begin: TokenStream2,
    config_ty: TokenStream2,
    guard: TokenStream2,
) -> TokenStream {
    let c = parse_macro_input!(input as Container);
    if let Err(e) = check_keys(&c.fields, &["min_width", "disabled"]) {
        return e.to_compile_error().into();
    }
    let label = match require_label(&c, macro_name) {
        Ok(l) => l,
        Err(e) => return e,
    };
    let body = body_tokens(&c);
    let min_width = find(&c.fields, "min_width")
        .map(|e| quote! { ((#e) as f32) })
        .unwrap_or_else(|| quote! { 0.0f32 });
    let disabled = find(&c.fields, "disabled")
        .map(|e| quote! { (#e) })
        .unwrap_or_else(|| quote! { false });
    quote! {
        if #begin(#label, #config_ty { min_width: #min_width, disabled: #disabled }) {
            let __fl_menu_level_end_guard = #guard::new();
            { #body }
        }
    }
    .into()
}

/// The menu bar (the MenuBar::begin/body/MenuBar::end pair). Like [`scroll`] - and unlike [`menu`] - its
/// body always runs: the strip is the persistent File/Edit/View surface, so it expands to a plain
/// MenuBar::begin(..); body; MenuBar::end(); with the tops ([`menubar_menu!`]) emitted in the body every frame.
/// Takes an optional positional id (the bar's stable identity) and a single flat field - z_index (0 =
/// default 1000) - plus the trailing { body } of tops.
#[proc_macro]
pub fn menubar(input: TokenStream) -> TokenStream {
    let c = parse_macro_input!(input as Container);
    if let Err(e) = check_keys(&c.fields, &["z_index"]) {
        return e.to_compile_error().into();
    }
    let id = id_tokens(&c);
    let body = body_tokens(&c);
    let z_index = find(&c.fields, "z_index")
        .map(|e| quote! { ((#e) as i16) })
        .unwrap_or_else(|| quote! { 0i16 });
    quote! {
        {
            ::flowi::MenuBar::begin(#id, ::flowi::MenuBarConfig { z_index: #z_index });
            let __fl_menubar_end_guard = ::flowi::MenuBarEndGuard::new();
            { #body }
        }
    }
    .into()
}

/// One top in the menu bar (the UiMenubarMenu::begin/body/UiMenubarMenu::end pair). Like [`submenu`] its body
/// runs only while the dropdown is open (a click on the top opens it): it expands to an
/// if UiMenubarMenu::begin(..) { body; UiMenubarMenu::end(); }. Takes a required positional label (the top's text
/// and identity) and flat fields - min_width (logical px; 0.0 = default 180) and disabled (bool) - plus the
/// trailing { body } of dropdown items.
#[proc_macro]
pub fn menubar_menu(input: TokenStream) -> TokenStream {
    expand_labeled_open_conditional(
        input,
        "menubar_menu",
        quote! { ::flowi::UiMenubarMenu::begin },
        quote! { ::flowi::MenuBarMenuConfig },
        quote! { ::flowi::MenubarMenuEndGuard },
    )
}

/// A parsed [`menu_check`] invocation: a positional label, a &mut bound value, and optional trailing fields.
struct MenuCheck {
    label: Expr,
    bound: Expr,
    fields: Vec<(Ident, Expr)>,
}

impl Parse for MenuCheck {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let label: Expr = input.call(Expr::parse_without_eager_brace)?;
        input.parse::<Token![,]>()?;
        let bound: Expr = input.call(Expr::parse_without_eager_brace)?;
        let fields = parse_trailing_fields(input)?;
        Ok(MenuCheck {
            label,
            bound,
            fields,
        })
    }
}

/// A parsed [`menu_radio`] invocation: a positional label, a &mut bound value, the option value, and optional
/// trailing fields.
struct MenuRadio {
    label: Expr,
    bound: Expr,
    value: Expr,
    fields: Vec<(Ident, Expr)>,
}

impl Parse for MenuRadio {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let label: Expr = input.call(Expr::parse_without_eager_brace)?;
        input.parse::<Token![,]>()?;
        let bound: Expr = input.call(Expr::parse_without_eager_brace)?;
        input.parse::<Token![,]>()?;
        let value: Expr = input.call(Expr::parse_without_eager_brace)?;
        let fields = parse_trailing_fields(input)?;
        Ok(MenuRadio {
            label,
            bound,
            value,
            fields,
        })
    }
}

/// Comma-separated sizing arguments for the [`fixed`] / [`grow`] / [`percent`] /
/// [`fit`] sub-macros.
struct SizingArgs(Vec<Expr>);

impl Parse for SizingArgs {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let p = Punctuated::<Expr, Token![,]>::parse_terminated(input)?;
        Ok(SizingArgs(p.into_iter().collect()))
    }
}

/// A min_max-armed axis (Fit / Grow): each of min / max defaults to 0.
fn min_max_axis(args: &[Expr], ty: TokenStream2, macro_name: &str) -> syn::Result<TokenStream2> {
    if args.len() > 2 {
        return Err(syn::Error::new(
            proc_macro2::Span::call_site(),
            format!("{macro_name}! takes at most two arguments (min, max)"),
        ));
    }
    if args.is_empty() {
        return Ok(if macro_name == "fit" {
            default_fit()
        } else {
            default_grow()
        });
    }
    let min = args
        .first()
        .map(|e| quote! { ((#e) as f32) })
        .unwrap_or_else(|| quote! { 0.0f32 });
    let max = args
        .get(1)
        .map(|e| quote! { ((#e) as f32) })
        .unwrap_or_else(|| quote! { 0.0f32 });
    Ok(quote! {
        ::flowi::LayoutSizingAxis {
            size: ::flowi::LayoutSizingAxisSize {
                min_max: ::flowi::LayoutSizingMinMax { min: #min, max: #max },
            },
            r#type: #ty,
        }
    })
}

/// A fixed axis: min == max == value. Exactly one argument.
#[proc_macro]
pub fn fixed(input: TokenStream) -> TokenStream {
    let args = parse_macro_input!(input as SizingArgs).0;
    if args.len() != 1 {
        return syn::Error::new(
            proc_macro2::Span::call_site(),
            "fixed! takes exactly one argument",
        )
        .to_compile_error()
        .into();
    }
    let v = &args[0];
    quote! {
        {
            let __v: f32 = (#v) as f32;
            ::flowi::LayoutSizingAxis {
                size: ::flowi::LayoutSizingAxisSize {
                    min_max: ::flowi::LayoutSizingMinMax { min: __v, max: __v },
                },
                r#type: ::flowi::LayoutSizingType::Fixed,
            }
        }
    }
    .into()
}

/// A grow axis (grow!(), grow!(min), or grow!(min, max)).
#[proc_macro]
pub fn grow(input: TokenStream) -> TokenStream {
    let args = parse_macro_input!(input as SizingArgs).0;
    match min_max_axis(&args, quote! { ::flowi::LayoutSizingType::Grow }, "grow") {
        Ok(t) => t.into(),
        Err(e) => e.to_compile_error().into(),
    }
}

/// A fit axis (fit!(), fit!(min), or fit!(min, max)).
#[proc_macro]
pub fn fit(input: TokenStream) -> TokenStream {
    let args = parse_macro_input!(input as SizingArgs).0;
    match min_max_axis(&args, quote! { ::flowi::LayoutSizingType::Fit }, "fit") {
        Ok(t) => t.into(),
        Err(e) => e.to_compile_error().into(),
    }
}

/// A percent axis: a fraction of the parent's size. Exactly one argument (0.0..=1.0).
#[proc_macro]
pub fn percent(input: TokenStream) -> TokenStream {
    let args = parse_macro_input!(input as SizingArgs).0;
    if args.len() != 1 {
        return syn::Error::new(
            proc_macro2::Span::call_site(),
            "percent! takes exactly one argument",
        )
        .to_compile_error()
        .into();
    }
    let v = &args[0];
    quote! {
        ::flowi::LayoutSizingAxis {
            size: ::flowi::LayoutSizingAxisSize { percent: ((#v) as f32) },
            r#type: ::flowi::LayoutSizingType::Percent,
        }
    }
    .into()
}

/// Parsed clip! arguments: bare-ident flags (each omitted = false) plus the only
/// named, non-bool field, scroll_pos:.
struct ClipArgs {
    horizontal: bool,
    vertical: bool,
    manual_scroll: bool,
    scroll_pos: Option<Expr>,
}

impl Parse for ClipArgs {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let mut out = ClipArgs {
            horizontal: false,
            vertical: false,
            manual_scroll: false,
            scroll_pos: None,
        };
        while !input.is_empty() {
            let key: Ident = input.parse()?;
            let ks = key.to_string();
            if input.peek(Token![:]) {
                input.parse::<Token![:]>()?;
                if ks != "scroll_pos" {
                    return Err(syn::Error::new(
                        key.span(),
                        format!(
                            "unknown clip! field `{ks}`; the only valued field is `scroll_pos`"
                        ),
                    ));
                }
                if out.scroll_pos.is_some() {
                    return Err(syn::Error::new(
                        key.span(),
                        "duplicate clip! field `scroll_pos`",
                    ));
                }
                out.scroll_pos = Some(input.call(Expr::parse_without_eager_brace)?);
            } else {
                let flag = match ks.as_str() {
                    "horizontal" => &mut out.horizontal,
                    "vertical" => &mut out.vertical,
                    "manual_scroll" => &mut out.manual_scroll,
                    _ => {
                        return Err(syn::Error::new(
                            key.span(),
                            format!("unknown clip! flag `{ks}`; expected `horizontal`, `vertical`, `manual_scroll`, or `scroll_pos:`"),
                        ))
                    }
                };
                if *flag {
                    return Err(syn::Error::new(
                        key.span(),
                        format!("duplicate clip! flag `{ks}`"),
                    ));
                }
                *flag = true;
            }
            if input.peek(Token![,]) {
                input.parse::<Token![,]>()?;
            } else {
                break;
            }
        }
        if !input.is_empty() {
            return Err(input.error("unexpected tokens in clip!"));
        }
        Ok(out)
    }
}

/// LayoutClip from bare-ident flags and an optional scroll_pos: - the macro
/// form of the clip field. `clip!(horizontal, manual_scroll, scroll_pos: [x, 0.0])`.
#[proc_macro]
pub fn clip(input: TokenStream) -> TokenStream {
    let args = parse_macro_input!(input as ClipArgs);
    let scroll_pos = match &args.scroll_pos {
        Some(e) => match vec2_tokens(e) {
            Ok(t) => t,
            Err(e) => return e.to_compile_error().into(),
        },
        None => quote! { ::flowi::Vec2 { x: 0.0, y: 0.0 } },
    };
    let (h, v, m) = (args.horizontal, args.vertical, args.manual_scroll);
    quote! {
        ::flowi::LayoutClip {
            horizontal: #h,
            vertical: #v,
            manual_scroll: #m,
            scroll_pos: #scroll_pos,
        }
    }
    .into()
}

/// LayoutBorder from a positional color and a uniform width - the macro form of
/// the border field. between_children defaults to 0
/// and the stroke to solid.
#[proc_macro]
pub fn border(input: TokenStream) -> TokenStream {
    let args = parse_macro_input!(input as SizingArgs).0;
    if args.len() != 2 {
        return syn::Error::new(
            proc_macro2::Span::call_site(),
            "border! takes exactly two arguments (color, width)",
        )
        .to_compile_error()
        .into();
    }
    let color = match color_tokens(&args[0]) {
        Ok(t) => t,
        Err(e) => return e.to_compile_error().into(),
    };
    let width = &args[1];
    quote! {
        {
            let __w: u16 = (#width);
            ::flowi::LayoutBorder {
                color: #color,
                width: ::flowi::BorderWidthConfig {
                    left: __w, right: __w, top: __w, bottom: __w, between_children: 0,
                },
                stroke: ::flowi::StrokeStyle { dash_len: 0.0, gap_len: 0.0 },
            }
        }
    }
    .into()
}

/// Comma-separated bare idents for [`align`].
struct AlignArgs(Vec<Ident>);

impl Parse for AlignArgs {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let p = Punctuated::<Ident, Token![,]>::parse_terminated(input)?;
        Ok(AlignArgs(p.into_iter().collect()))
    }
}

fn align_x(id: &Ident) -> syn::Result<TokenStream2> {
    match id.to_string().as_str() {
        "Left" => Ok(quote! { ::flowi::LayoutAlignmentX::Left }),
        "Right" => Ok(quote! { ::flowi::LayoutAlignmentX::Right }),
        "Center" => Ok(quote! { ::flowi::LayoutAlignmentX::Center }),
        other => Err(syn::Error::new(
            id.span(),
            format!("unknown x alignment `{other}`; expected Left, Right, or Center"),
        )),
    }
}

fn align_y(id: &Ident) -> syn::Result<TokenStream2> {
    match id.to_string().as_str() {
        "Top" => Ok(quote! { ::flowi::LayoutAlignmentY::Top }),
        "Bottom" => Ok(quote! { ::flowi::LayoutAlignmentY::Bottom }),
        "Center" => Ok(quote! { ::flowi::LayoutAlignmentY::Center }),
        other => Err(syn::Error::new(
            id.span(),
            format!("unknown y alignment `{other}`; expected Top, Bottom, or Center"),
        )),
    }
}

/// LayoutChildAlignment from positional bare-ident variants - the macro form of
/// the align field. align!(Center, Top): first arg is the x variant, second the y.
#[proc_macro]
pub fn align(input: TokenStream) -> TokenStream {
    let ids = parse_macro_input!(input as AlignArgs).0;
    if ids.len() != 2 {
        return syn::Error::new(
            proc_macro2::Span::call_site(),
            "align! takes exactly two arguments (x, y)",
        )
        .to_compile_error()
        .into();
    }
    let x = match align_x(&ids[0]) {
        Ok(t) => t,
        Err(e) => return e.to_compile_error().into(),
    };
    let y = match align_y(&ids[1]) {
        Ok(t) => t,
        Err(e) => return e.to_compile_error().into(),
    };
    quote! { ::flowi::LayoutChildAlignment { x: #x, y: #y } }.into()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fallback_id_is_file_line_column() {
        let c = Container {
            id: None,
            fields: Vec::new(),
            body: None,
        };
        let expected = quote! {
            ::core::concat!(::core::file!(), ":", ::core::line!(), ":", ::core::column!())
        };
        assert_eq!(id_tokens(&c).to_string(), expected.to_string());
    }

    #[test]
    fn explicit_id_passes_through() {
        let e: Expr = syn::parse_str("\"root\"").unwrap();
        let c = Container {
            id: Some(e),
            fields: Vec::new(),
            body: None,
        };
        assert_eq!(id_tokens(&c).to_string(), quote! { "root" }.to_string());
    }
}
