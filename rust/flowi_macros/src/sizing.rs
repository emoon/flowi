//! Expansion of the fixed, grow, fit, percent and border sub-macros, which all take comma-separated
//! expression arguments.

use proc_macro2::TokenStream as TokenStream2;
use quote::quote;
use syn::parse::{Parse, ParseStream};
use syn::punctuated::Punctuated;
use syn::{Expr, Token};

use crate::emit::call_site_error;
use crate::layout::{color_tokens, default_fit, default_grow};

/// Comma-separated arguments for the sub-macros.
pub(crate) struct SizingArgs(Vec<Expr>);

impl Parse for SizingArgs {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let p = Punctuated::<Expr, Token![,]>::parse_terminated(input)?;
        Ok(SizingArgs(p.into_iter().collect()))
    }
}

/// Which min_max-armed axis a macro builds.
#[derive(Clone, Copy)]
pub(crate) enum MinMaxAxis {
    Fit,
    Grow,
}

impl SizingArgs {
    /// A fixed axis: min == max == value. Exactly one argument.
    pub(crate) fn expand_fixed(&self) -> syn::Result<TokenStream2> {
        let v = self.single_arg("fixed")?;
        Ok(quote! {
            {
                let __v: f32 = (#v) as f32;
                ::flowi::LayoutSizingAxis {
                    size: ::flowi::LayoutSizingAxisSize {
                        min_max: ::flowi::LayoutSizingMinMax { min: __v, max: __v },
                    },
                    r#type: ::flowi::LayoutSizingType::Fixed,
                }
            }
        })
    }

    /// A percent axis: a fraction of the parent's size. Exactly one argument (0.0..=1.0).
    pub(crate) fn expand_percent(&self) -> syn::Result<TokenStream2> {
        let v = self.single_arg("percent")?;
        Ok(quote! {
            ::flowi::LayoutSizingAxis {
                size: ::flowi::LayoutSizingAxisSize { percent: ((#v) as f32) },
                r#type: ::flowi::LayoutSizingType::Percent,
            }
        })
    }

    /// A Fit / Grow axis (`m!()`, `m!(min)` or `m!(min, max)`): each of min / max defaults to 0.
    pub(crate) fn expand_min_max(&self, axis: MinMaxAxis) -> syn::Result<TokenStream2> {
        let (macro_name, default, ty) = match axis {
            MinMaxAxis::Fit => ("fit", default_fit(), quote! { ::flowi::LayoutSizingType::Fit }),
            MinMaxAxis::Grow => ("grow", default_grow(), quote! { ::flowi::LayoutSizingType::Grow }),
        };
        if self.0.len() > 2 {
            return Err(call_site_error(format!(
                "{macro_name}! takes at most two arguments (min, max)"
            )));
        }
        if self.0.is_empty() {
            return Ok(default);
        }
        let min = self.f32_arg_or_zero(0);
        let max = self.f32_arg_or_zero(1);
        Ok(quote! {
            ::flowi::LayoutSizingAxis {
                size: ::flowi::LayoutSizingAxisSize {
                    min_max: ::flowi::LayoutSizingMinMax { min: #min, max: #max },
                },
                r#type: #ty,
            }
        })
    }

    /// A LayoutBorder from a positional color and a uniform width. between_children defaults to 0
    /// and the stroke to solid.
    pub(crate) fn expand_border(&self) -> syn::Result<TokenStream2> {
        let [color, width] = &self.0[..] else {
            return Err(call_site_error(
                "border! takes exactly two arguments (color, width)",
            ));
        };
        let color = color_tokens(color)?;
        Ok(quote! {
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
        })
    }

    fn single_arg(&self, macro_name: &str) -> syn::Result<&Expr> {
        match &self.0[..] {
            [only] => Ok(only),
            _ => Err(call_site_error(format!(
                "{macro_name}! takes exactly one argument"
            ))),
        }
    }

    fn f32_arg_or_zero(&self, index: usize) -> TokenStream2 {
        self.0
            .get(index)
            .map(|e| quote! { ((#e) as f32) })
            .unwrap_or_else(|| quote! { 0.0f32 })
    }
}
