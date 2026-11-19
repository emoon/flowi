//! Expansion of the layout, row, col, placed, floating and scroll container macros.

use proc_macro2::TokenStream as TokenStream2;
use quote::quote;
use syn::Expr;

use crate::container::Container;
use crate::emit::guarded_body;
use crate::fields::Fields;

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

/// Which layout macro is expanding: the preset directions (row/col) take no direction field.
#[derive(Clone, Copy)]
pub(crate) enum LayoutKind {
    Layout,
    Row,
    Col,
    Placed,
}

impl LayoutKind {
    fn allowed_fields(self) -> &'static [&'static str] {
        match self {
            LayoutKind::Placed => FIELDS_PLACED,
            LayoutKind::Layout => FIELDS_WITH_DIR,
            LayoutKind::Row | LayoutKind::Col => FIELDS,
        }
    }

    fn direction(self, fields: &Fields) -> TokenStream2 {
        match self {
            LayoutKind::Row => quote! { ::flowi::LayoutDirection::LeftToRight },
            LayoutKind::Col => quote! { ::flowi::LayoutDirection::TopToBottom },
            LayoutKind::Layout | LayoutKind::Placed => fields.paren_or("direction", default_direction),
        }
    }

    fn placed_tokens(self, fields: &Fields) -> syn::Result<TokenStream2> {
        if !matches!(self, LayoutKind::Placed) {
            return Ok(default_field(quote! { placed }));
        }
        let offset = fields.try_lower_or("offset", vec2_tokens, || {
            default_field(quote! { placed.offset })
        })?;
        Ok(quote! { ::flowi::LayoutPlaced { offset: #offset, enabled: true } })
    }
}

impl Container {
    /// Emit start; configure; guard; { body } for a layout container. The end
    /// call lives in the guard's Drop, so the container closes even when the body
    /// panics or returns early.
    pub(crate) fn expand_layout(&self, kind: LayoutKind) -> syn::Result<TokenStream2> {
        let config = self.layout_config(kind)?;
        let id = self.id_tokens();
        let guarded = self.end_guarded_body();
        Ok(quote! {
            {
                ::flowi::Layout::start(#id);
                ::flowi::Layout::configure(#config);
                #guarded
            }
        })
    }

    /// A floating element (modal dialog, popup, tooltip), closed with the ordinary layout container end.
    pub(crate) fn expand_floating(&self) -> syn::Result<TokenStream2> {
        let config = self.floating_config()?;
        let id = self.id_tokens();
        let guarded = self.end_guarded_body();
        Ok(quote! {
            {
                ::flowi::Floating::start(#id);
                ::flowi::Floating::configure(#config);
                #guarded
            }
        })
    }

    /// A scroll area: a clipping container with managed scroll state.
    pub(crate) fn expand_scroll(&self) -> syn::Result<TokenStream2> {
        let config = self.scroll_config()?;
        let id = self.id_tokens();
        let guarded = guarded_body(
            quote! { __fl_scroll_end_guard },
            quote! { ::flowi::ScrollEndGuard },
            self.body_tokens(),
        );
        Ok(quote! {
            {
                ::flowi::Scroll::area_begin(#id, #config);
                #guarded
            }
        })
    }

    fn end_guarded_body(&self) -> TokenStream2 {
        guarded_body(
            quote! { __fl_layout_end_guard },
            quote! { ::flowi::LayoutEndGuard },
            self.body_tokens(),
        )
    }

    /// Build the LayoutConfig literal for [`layout`] / [`row`] / [`col`] / [`placed`].
    fn layout_config(&self, kind: LayoutKind) -> syn::Result<TokenStream2> {
        let f = &self.fields;
        f.check_keys(kind.allowed_fields())?;

        let width = f.bare_or("width", default_fit);
        let height = f.bare_or("height", default_fit);
        let padding = f.lower_or("padding", padding_tokens, zero_padding);
        let gap = f.paren_or("gap", default_gap);
        let radius = f.lower_or("radius", radius_tokens, zero_radius);
        let color = f.try_lower_or("color", color_tokens, transparent)?;
        let clip = f.paren_or("clip", default_clip);
        let border = f.paren_or("border", default_border);
        let child_alignment = f.paren_or("align", default_align);
        let direction = kind.direction(f);
        let placed = kind.placed_tokens(f)?;

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
    fn floating_config(&self) -> syn::Result<TokenStream2> {
        let f = &self.fields;
        f.check_keys(&[
            "width",
            "height",
            "offset",
            "z_index",
            "attach",
            "pointer_capture",
            "element_attach",
            "parent_attach",
        ])?;

        let width = f.bare_or("width", default_fit);
        let height = f.bare_or("height", default_fit);
        let offset = f.try_lower_or("offset", vec2_tokens, || {
            quote! { ::flowi::Vec2 { x: 0.0, y: 0.0 } }
        })?;
        let z_index = f.paren_or("z_index", || quote! { 0i16 });
        let attach = f.paren_or("attach", || quote! { ::flowi::FloatingAttachTo::Root });
        let pointer_capture = f.paren_or("pointer_capture", || {
            quote! { ::flowi::FloatingPointerCapture::Capture }
        });
        let element_attach = f.paren_or("element_attach", || {
            quote! { ::flowi::FloatingAttachPoint::LeftTop }
        });
        let parent_attach = f.paren_or("parent_attach", || {
            quote! { ::flowi::FloatingAttachPoint::LeftTop }
        });

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
    fn scroll_config(&self) -> syn::Result<TokenStream2> {
        let f = &self.fields;
        f.check_keys(&[
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
        ])?;

        let width = f.bare_or("width", default_grow);
        let height = f.bare_or("height", default_grow);
        let direction = f.paren_or("direction", || {
            quote! { ::flowi::LayoutDirection::TopToBottom }
        });
        let gap = f.paren_or("gap", default_gap);
        let padding = f.lower_or("padding", padding_tokens, zero_padding);
        let color = f.try_lower_or("color", color_tokens, transparent)?;
        let radius = f.lower_or("radius", radius_tokens, zero_radius);
        let scrollbar = f.paren_or("scrollbar", || quote! { false });
        let scrollbar_mode = f.paren_or("scrollbar_mode", || {
            quote! { ::flowi::ScrollbarDisplayMode::Auto }
        });
        let smooth = f.paren_or("smooth", || quote! { true });
        let speed = f.paren_or("speed", || quote! { 30.0f32 });

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

pub(crate) fn default_fit() -> TokenStream2 {
    quote! { ::flowi::LayoutSizingAxis::FIT }
}

pub(crate) fn default_grow() -> TokenStream2 {
    quote! { ::flowi::LayoutSizingAxis::GROW }
}

/// Color from a `[r, g, b]` / `[r, g, b, a]` array literal (alpha defaults to
/// 255), or any other expression passed through as a full Color value.
pub(crate) fn color_tokens(expr: &Expr) -> syn::Result<TokenStream2> {
    let Expr::Array(arr) = expr else {
        return Ok(quote! { (#expr) });
    };
    let elems: Vec<&Expr> = arr.elems.iter().collect();
    match elems[..] {
        [r, g, b] => Ok(quote! { ::flowi::Color { r: #r, g: #g, b: #b, a: 255 } }),
        [r, g, b, a] => Ok(quote! { ::flowi::Color { r: #r, g: #g, b: #b, a: #a } }),
        _ => Err(syn::Error::new(
            arr.bracket_token.span.join(),
            "color array must have 3 ([r, g, b]) or 4 ([r, g, b, a]) elements",
        )),
    }
}

/// Vec2 from an `[x, y]` array literal, or any other expression passed through.
pub(crate) fn vec2_tokens(expr: &Expr) -> syn::Result<TokenStream2> {
    let Expr::Array(arr) = expr else {
        return Ok(quote! { (#expr) });
    };
    let elems: Vec<&Expr> = arr.elems.iter().collect();
    match elems[..] {
        [x, y] => Ok(quote! { ::flowi::Vec2 { x: ((#x) as f32), y: ((#y) as f32) } }),
        _ => Err(syn::Error::new(
            arr.bracket_token.span.join(),
            "offset array must have 2 ([x, y]) elements",
        )),
    }
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
