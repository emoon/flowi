//! The parsed container invocation and the pieces of its expansion every container macro shares.

use proc_macro2::TokenStream as TokenStream2;
use quote::quote;
use syn::parse::{Parse, ParseStream};
use syn::{braced, token, Expr};

use crate::emit::call_site_error;
use crate::fields::Fields;

/// One parsed container invocation: an optional positional id, flat name: value
/// fields, and an optional trailing { body } block.
pub(crate) struct Container {
    pub(crate) id: Option<Expr>,
    pub(crate) fields: Fields,
    pub(crate) body: Option<TokenStream2>,
}

impl Parse for Container {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let id = Self::parse_id(input)?;
        let fields = Fields::parse_leading(input)?;
        let body = Self::parse_body(input)?;

        if !input.is_empty() {
            return Err(input.error("unexpected tokens after container body"));
        }

        Ok(Container { id, fields, body })
    }
}

impl Container {
    /// Optional positional id, parsed without eager-brace so a trailing { ... }
    /// body is never mistaken for a struct literal.
    fn parse_id(input: ParseStream) -> syn::Result<Option<Expr>> {
        if Fields::at_body_or_end(input) || Fields::at_pair(input) {
            return Ok(None);
        }
        let e: Expr = input.call(Expr::parse_without_eager_brace)?;
        Fields::eat_comma(input)?;
        Ok(Some(e))
    }

    /// An optional `{ ... }` body block, as its inner tokens.
    pub(crate) fn parse_body(input: ParseStream) -> syn::Result<Option<TokenStream2>> {
        if !input.peek(token::Brace) {
            return Ok(None);
        }
        let content;
        braced!(content in input);
        Ok(Some(content.parse::<TokenStream2>()?))
    }

    /// The container's positional id, or a compile error naming the macro when it is omitted.
    pub(crate) fn require_label(&self, macro_name: &str) -> syn::Result<TokenStream2> {
        match &self.id {
            Some(e) => Ok(quote! { #e }),
            None => Err(call_site_error(format!("{macro_name}! requires a label"))),
        }
    }

    /// The container id: the positional expression, or a synthesised stable
    /// file:line:column string when omitted. Both resolve to an flowi::LayoutId via
    /// Into at the call site. The column keeps two id-less invocations on one source line distinct.
    pub(crate) fn id_tokens(&self) -> TokenStream2 {
        match &self.id {
            Some(e) => quote! { #e },
            None => {
                quote! { ::core::concat!(::core::file!(), ":", ::core::line!(), ":", ::core::column!()) }
            }
        }
    }

    /// The body block, defaulting to empty. Wrapped by the caller in its own { } so a
    /// trailing expression is the block's (discarded) value rather than a stray statement.
    pub(crate) fn body_tokens(&self) -> TokenStream2 {
        self.body.clone().unwrap_or_default()
    }
}
