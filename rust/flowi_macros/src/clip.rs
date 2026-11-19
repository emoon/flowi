//! The clip sub-macro: bare-ident flags plus an optional `scroll_pos:` value.

use proc_macro2::TokenStream as TokenStream2;
use quote::quote;
use syn::parse::{Parse, ParseStream};
use syn::{Expr, Ident, Token};

use crate::fields::Fields;
use crate::layout::vec2_tokens;

/// Parsed clip! arguments: bare-ident flags (each omitted = false) plus the only
/// named, non-bool field, scroll_pos:.
pub(crate) struct ClipArgs {
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
            if input.peek(Token![:]) {
                input.parse::<Token![:]>()?;
                out.set_scroll_pos(&key, input)?;
            } else {
                out.set_flag(&key)?;
            }
            if !Fields::eat_comma(input)? {
                break;
            }
        }
        if !input.is_empty() {
            return Err(input.error("unexpected tokens in clip!"));
        }
        Ok(out)
    }
}

impl ClipArgs {
    fn set_scroll_pos(&mut self, key: &Ident, input: ParseStream) -> syn::Result<()> {
        if key != "scroll_pos" {
            return Err(syn::Error::new(
                key.span(),
                format!(
                    "unknown clip! field `{key}`; the only valued field is `scroll_pos`"
                ),
            ));
        }
        if self.scroll_pos.is_some() {
            return Err(syn::Error::new(
                key.span(),
                "duplicate clip! field `scroll_pos`",
            ));
        }
        self.scroll_pos = Some(input.call(Expr::parse_without_eager_brace)?);
        Ok(())
    }

    fn set_flag(&mut self, key: &Ident) -> syn::Result<()> {
        let ks = key.to_string();
        let flag = match ks.as_str() {
            "horizontal" => &mut self.horizontal,
            "vertical" => &mut self.vertical,
            "manual_scroll" => &mut self.manual_scroll,
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
        Ok(())
    }

    /// A LayoutClip from the parsed flags and optional scroll_pos.
    pub(crate) fn expand(&self) -> syn::Result<TokenStream2> {
        let scroll_pos = match &self.scroll_pos {
            Some(e) => vec2_tokens(e)?,
            None => quote! { ::flowi::Vec2 { x: 0.0, y: 0.0 } },
        };
        let (h, v, m) = (self.horizontal, self.vertical, self.manual_scroll);
        Ok(quote! {
            ::flowi::LayoutClip {
                horizontal: #h,
                vertical: #v,
                manual_scroll: #m,
                scroll_pos: #scroll_pos,
            }
        })
    }
}
