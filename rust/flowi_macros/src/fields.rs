//! The flat `name: value` field list every macro shares, and the token builders that read it.

use proc_macro2::TokenStream as TokenStream2;
use quote::quote;
use syn::parse::ParseStream;
use syn::{Expr, Ident, Token};

/// The parsed `name: value` pairs of one invocation, in source order.
#[derive(Default)]
pub(crate) struct Fields(Vec<(Ident, Expr)>);

impl Fields {
    pub(crate) fn is_empty(&self) -> bool {
        self.0.is_empty()
    }

    /// Parse `name: value` pairs separated by commas, stopping at the end of input or a `{` body.
    pub(crate) fn parse_leading(input: ParseStream) -> syn::Result<Self> {
        let mut pairs = Vec::new();
        while !Self::at_body_or_end(input) {
            pairs.push(Self::parse_pair(input)?);
            if !Self::eat_comma(input)? {
                break;
            }
        }
        Ok(Fields(pairs))
    }

    /// Parse an optional trailing `, name: value, ...` list (no body block). A trailing comma is
    /// tolerated; any non-field remainder is an error.
    pub(crate) fn parse_trailing(input: ParseStream) -> syn::Result<Self> {
        let mut pairs = Vec::new();
        while Self::eat_comma(input)? {
            if input.is_empty() {
                break;
            }
            pairs.push(Self::parse_pair(input)?);
        }
        if !input.is_empty() {
            return Err(input.error("unexpected tokens after menu item fields"));
        }
        Ok(Fields(pairs))
    }

    /// Whether the next token is a `{` body or nothing at all.
    pub(crate) fn at_body_or_end(input: ParseStream) -> bool {
        input.is_empty() || input.peek(syn::token::Brace)
    }

    /// Whether the next tokens read `name:`.
    pub(crate) fn at_pair(input: ParseStream) -> bool {
        input.peek(Ident) && input.peek2(Token![:])
    }

    /// Consume a comma if one is next; whether it was.
    pub(crate) fn eat_comma(input: ParseStream) -> syn::Result<bool> {
        if input.peek(Token![,]) {
            input.parse::<Token![,]>()?;
            return Ok(true);
        }
        Ok(false)
    }

    fn parse_pair(input: ParseStream) -> syn::Result<(Ident, Expr)> {
        let key: Ident = input.parse()?;
        input.parse::<Token![:]>()?;
        let val: Expr = input.call(Expr::parse_without_eager_brace)?;
        Ok((key, val))
    }

    /// First field value matching name (fields are validated unique by [`Fields::check_keys`]).
    pub(crate) fn find(&self, name: &str) -> Option<&Expr> {
        self.0.iter().find(|(k, _)| *k == name).map(|(_, v)| v)
    }

    pub(crate) fn check_keys(&self, allowed: &[&str]) -> syn::Result<()> {
        let mut seen: Vec<String> = Vec::new();
        for (k, _) in &self.0 {
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

    /// The field's expression as is, or `default`.
    pub(crate) fn bare_or(&self, name: &str, default: impl FnOnce() -> TokenStream2) -> TokenStream2 {
        self.find(name).map(|e| quote! { #e }).unwrap_or_else(default)
    }

    /// The field's expression parenthesised, or `default`.
    pub(crate) fn paren_or(&self, name: &str, default: impl FnOnce() -> TokenStream2) -> TokenStream2 {
        self.find(name).map(|e| quote! { (#e) }).unwrap_or_else(default)
    }

    /// The field's expression cast to `ty`, or `default`.
    pub(crate) fn cast_or(
        &self,
        name: &str,
        ty: TokenStream2,
        default: impl FnOnce() -> TokenStream2,
    ) -> TokenStream2 {
        self.find(name)
            .map(|e| quote! { ((#e) as #ty) })
            .unwrap_or_else(default)
    }

    /// The field lowered by `lower`, or `default`.
    pub(crate) fn lower_or(
        &self,
        name: &str,
        lower: fn(&Expr) -> TokenStream2,
        default: impl FnOnce() -> TokenStream2,
    ) -> TokenStream2 {
        self.find(name).map(lower).unwrap_or_else(default)
    }

    /// The field lowered by a fallible `lower`, or `default`.
    pub(crate) fn try_lower_or(
        &self,
        name: &str,
        lower: fn(&Expr) -> syn::Result<TokenStream2>,
        default: impl FnOnce() -> TokenStream2,
    ) -> syn::Result<TokenStream2> {
        match self.find(name) {
            Some(e) => lower(e),
            None => Ok(default()),
        }
    }

    /// Build the ::flowi::MenuItemConfig { ... } literal shared by the leaf item macros. The caller
    /// validates the field names via [`Fields::check_keys`] first; an absent field is zeroed.
    pub(crate) fn menu_item_config_tokens(&self) -> TokenStream2 {
        let shortcut = self.paren_or("shortcut", || quote! { "" });
        let close_policy = self.paren_or("close_policy", || {
            quote! { ::flowi::MenuClosePolicy::Default }
        });
        let disabled = self.paren_or("disabled", || quote! { false });
        quote! {
            ::flowi::MenuItemConfig {
                close_policy: #close_policy,
                shortcut: #shortcut,
                disabled: #disabled,
            }
        }
    }
}
