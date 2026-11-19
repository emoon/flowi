//! The align sub-macro: a LayoutChildAlignment from two bare-ident variants.

use proc_macro2::{Span, TokenStream as TokenStream2};
use quote::{format_ident, quote};
use syn::parse::{Parse, ParseStream};
use syn::punctuated::Punctuated;
use syn::{Ident, Token};

use crate::emit::call_site_error;

/// Comma-separated bare idents for [`align`](crate::align).
pub(crate) struct AlignArgs(Vec<Ident>);

impl Parse for AlignArgs {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let p = Punctuated::<Ident, Token![,]>::parse_terminated(input)?;
        Ok(AlignArgs(p.into_iter().collect()))
    }
}

/// One alignment axis: its name in diagnostics, its enum, and the variants it accepts.
struct Axis {
    name: &'static str,
    ty: &'static str,
    variants: [&'static str; 3],
}

const X: Axis = Axis {
    name: "x",
    ty: "LayoutAlignmentX",
    variants: ["Left", "Right", "Center"],
};

const Y: Axis = Axis {
    name: "y",
    ty: "LayoutAlignmentY",
    variants: ["Top", "Bottom", "Center"],
};

impl Axis {
    fn variant(&self, id: &Ident) -> syn::Result<TokenStream2> {
        let name = id.to_string();
        if !self.variants.contains(&name.as_str()) {
            let [first, second, last] = self.variants;
            return Err(syn::Error::new(
                id.span(),
                format!(
                    "unknown {} alignment `{name}`; expected {first}, {second}, or {last}",
                    self.name
                ),
            ));
        }
        let ty = format_ident!("{}", self.ty);
        let variant = Ident::new(&name, Span::call_site());
        Ok(quote! { ::flowi::#ty::#variant })
    }
}

impl AlignArgs {
    /// align!(Center, Top): first arg is the x variant, second the y.
    pub(crate) fn expand(&self) -> syn::Result<TokenStream2> {
        let [x, y] = &self.0[..] else {
            return Err(call_site_error("align! takes exactly two arguments (x, y)"));
        };
        let x = X.variant(x)?;
        let y = Y.variant(y)?;
        Ok(quote! { ::flowi::LayoutChildAlignment { x: #x, y: #y } })
    }
}
