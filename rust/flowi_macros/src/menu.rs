//! Expansion of the menu, menu bar and menu item macros.

use proc_macro2::TokenStream as TokenStream2;
use quote::quote;
use syn::parse::{Parse, ParseStream};
use syn::{Expr, Token};

use crate::container::Container;
use crate::emit::{call_site_error, guarded_body};
use crate::fields::Fields;

const ITEM_FIELDS: &[&str] = &["shortcut", "close_policy", "disabled"];

/// The nested-menu macros that open a level only while expanded, and the types each lowers to.
pub(crate) struct OpenConditional {
    pub(crate) macro_name: &'static str,
    pub(crate) begin: TokenStream2,
    pub(crate) config_ty: TokenStream2,
    pub(crate) guard: TokenStream2,
}

impl OpenConditional {
    pub(crate) fn submenu() -> Self {
        OpenConditional {
            macro_name: "submenu",
            begin: quote! { ::flowi::UiSubmenu::begin },
            config_ty: quote! { ::flowi::SubmenuConfig },
            guard: quote! { ::flowi::SubmenuEndGuard },
        }
    }

    pub(crate) fn menubar_menu() -> Self {
        OpenConditional {
            macro_name: "menubar_menu",
            begin: quote! { ::flowi::UiMenubarMenu::begin },
            config_ty: quote! { ::flowi::MenuBarMenuConfig },
            guard: quote! { ::flowi::MenubarMenuEndGuard },
        }
    }
}

fn menu_end_guarded_body(body: TokenStream2) -> TokenStream2 {
    guarded_body(
        quote! { __fl_menu_end_guard },
        quote! { ::flowi::MenuEndGuard },
        body,
    )
}

impl Container {
    /// A context menu: `if Menu::begin(..) { body }`, with a guard closing it on every exit path.
    pub(crate) fn expand_menu(&self) -> syn::Result<TokenStream2> {
        self.fields.check_keys(&["min_width", "z_index"])?;
        let id = self.id_tokens();
        let min_width = self.min_width_tokens();
        let z_index = self.z_index_tokens();
        let guarded = menu_end_guarded_body(self.body_tokens());
        Ok(quote! {
            if ::flowi::Menu::begin(#id, ::flowi::MenuConfig { min_width: #min_width, z_index: #z_index }) {
                #guarded
            }
        })
    }

    /// One menu action item, expanding to `menu_item_with` and returning its Signal.
    pub(crate) fn expand_menu_item(&self) -> syn::Result<TokenStream2> {
        if self.body.is_some() {
            return Err(call_site_error("menu_item! takes no body block"));
        }
        let label = self.require_label("menu_item")?;
        self.fields.check_keys(ITEM_FIELDS)?;
        let config = self.fields.menu_item_config_tokens();
        Ok(quote! {
            ::flowi::menu_item_with(#label, #config)
        })
    }

    pub(crate) fn expand_menu_separator(&self) -> syn::Result<TokenStream2> {
        if self.has_arguments() {
            return Err(call_site_error("menu_separator! takes no arguments"));
        }
        Ok(quote! { ::flowi::Menu::separator() })
    }

    pub(crate) fn expand_menu_header(&self) -> syn::Result<TokenStream2> {
        if self.has_fields_or_body() {
            return Err(call_site_error("menu_header! takes only a label"));
        }
        let label = self.require_label("menu_header")?;
        Ok(quote! { ::flowi::Menu::header(#label) })
    }

    /// Shared expansion for the label-plus-min_width/disabled open-conditional menu macros:
    /// `if <begin>(label, <Config> { min_width, disabled }) { let _g = <guard>::new(); body }`. The body
    /// runs only while the nested level is open, and the guard closes it on every exit path.
    pub(crate) fn expand_open_conditional(&self, level: OpenConditional) -> syn::Result<TokenStream2> {
        self.fields.check_keys(&["min_width", "disabled"])?;
        let label = self.require_label(level.macro_name)?;
        let min_width = self.min_width_tokens();
        let disabled = self.fields.paren_or("disabled", || quote! { false });
        let OpenConditional { begin, config_ty, guard, .. } = level;
        let guarded = guarded_body(quote! { __fl_menu_level_end_guard }, guard, self.body_tokens());
        Ok(quote! {
            if #begin(#label, #config_ty { min_width: #min_width, disabled: #disabled }) {
                #guarded
            }
        })
    }

    /// The menu bar: unlike [`Container::expand_menu`] its body always runs, between begin and the guard's end.
    pub(crate) fn expand_menubar(&self) -> syn::Result<TokenStream2> {
        self.fields.check_keys(&["z_index"])?;
        let id = self.id_tokens();
        let z_index = self.z_index_tokens();
        let guarded = guarded_body(
            quote! { __fl_menubar_end_guard },
            quote! { ::flowi::MenuBarEndGuard },
            self.body_tokens(),
        );
        Ok(quote! {
            {
                ::flowi::MenuBar::begin(#id, ::flowi::MenuBarConfig { z_index: #z_index });
                #guarded
            }
        })
    }

    fn has_fields_or_body(&self) -> bool {
        !self.fields.is_empty() || self.body.is_some()
    }

    fn has_arguments(&self) -> bool {
        self.id.is_some() || self.has_fields_or_body()
    }

    fn min_width_tokens(&self) -> TokenStream2 {
        self.fields
            .cast_or("min_width", quote! { f32 }, || quote! { 0.0f32 })
    }

    fn z_index_tokens(&self) -> TokenStream2 {
        self.fields
            .cast_or("z_index", quote! { i16 }, || quote! { 0i16 })
    }
}

/// A parsed `menu_region!` invocation: two positional ids and an optional trailing body block.
pub(crate) struct MenuRegion {
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
        Fields::eat_comma(input)?;
        let body = Container::parse_body(input)?;
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

impl MenuRegion {
    /// A right-click region: always runs its body, with a guard ending the region on every exit path.
    pub(crate) fn expand(self) -> syn::Result<TokenStream2> {
        let MenuRegion { region_id, menu_id, body } = self;
        let guarded = guarded_body(
            quote! { __fl_menu_region_end_guard },
            quote! { ::flowi::MenuRegionEndGuard },
            body.unwrap_or_default(),
        );
        Ok(quote! {
            {
                ::flowi::Menu::region_begin(#region_id, #menu_id);
                #guarded
            }
        })
    }
}

/// A parsed `menu_check!` invocation: a positional label, a &mut bound value, and optional trailing fields.
pub(crate) struct MenuCheck {
    label: Expr,
    bound: Expr,
    fields: Fields,
}

impl Parse for MenuCheck {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let label: Expr = input.call(Expr::parse_without_eager_brace)?;
        input.parse::<Token![,]>()?;
        let bound: Expr = input.call(Expr::parse_without_eager_brace)?;
        let fields = Fields::parse_trailing(input)?;
        Ok(MenuCheck {
            label,
            bound,
            fields,
        })
    }
}

impl MenuCheck {
    pub(crate) fn expand(self) -> syn::Result<TokenStream2> {
        self.fields.check_keys(ITEM_FIELDS)?;
        let MenuCheck { label, bound, .. } = &self;
        let config = self.fields.menu_item_config_tokens();
        Ok(quote! {
            ::flowi::menu_check_with(#label, #bound, #config)
        })
    }
}

/// A parsed `menu_radio!` invocation: a positional label, a &mut bound value, the option value, and optional
/// trailing fields.
pub(crate) struct MenuRadio {
    label: Expr,
    bound: Expr,
    value: Expr,
    fields: Fields,
}

impl Parse for MenuRadio {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let label: Expr = input.call(Expr::parse_without_eager_brace)?;
        input.parse::<Token![,]>()?;
        let bound: Expr = input.call(Expr::parse_without_eager_brace)?;
        input.parse::<Token![,]>()?;
        let value: Expr = input.call(Expr::parse_without_eager_brace)?;
        let fields = Fields::parse_trailing(input)?;
        Ok(MenuRadio {
            label,
            bound,
            value,
            fields,
        })
    }
}

impl MenuRadio {
    pub(crate) fn expand(self) -> syn::Result<TokenStream2> {
        self.fields.check_keys(ITEM_FIELDS)?;
        let MenuRadio { label, bound, value, .. } = &self;
        let config = self.fields.menu_item_config_tokens();
        Ok(quote! {
            ::flowi::menu_radio_with(#label, #bound, #value, #config)
        })
    }
}
