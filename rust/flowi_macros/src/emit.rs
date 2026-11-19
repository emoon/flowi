//! Shared plumbing from a parsed invocation to the expanded tokens.

use proc_macro::TokenStream;
use proc_macro2::{Span, TokenStream as TokenStream2};
use quote::quote;
use syn::parse::Parse;

/// Parse `input` as `T` and expand it; a parse or expansion error becomes a `compile_error!`.
pub(crate) fn expand<T: Parse>(
    input: TokenStream,
    expand_parsed: impl FnOnce(T) -> syn::Result<TokenStream2>,
) -> TokenStream {
    syn::parse::<T>(input)
        .and_then(expand_parsed)
        .unwrap_or_else(|e| e.to_compile_error())
        .into()
}

/// An error reported at the macro call site.
pub(crate) fn call_site_error(message: impl std::fmt::Display) -> syn::Error {
    syn::Error::new(Span::call_site(), message)
}

/// `let <var> = <guard>::new(); { body }`: the guard's Drop closes the scope on every exit path, and
/// the inner block makes a trailing expression the block's (discarded) value, not a stray statement.
pub(crate) fn guarded_body(
    guard_var: TokenStream2,
    guard: TokenStream2,
    body: TokenStream2,
) -> TokenStream2 {
    quote! {
        let #guard_var = #guard::new();
        { #body }
    }
}
