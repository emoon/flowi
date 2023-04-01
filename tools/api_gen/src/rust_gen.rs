use crate::api_parser::*;
use heck::{ToShoutySnakeCase, ToSnakeCase};
use std::borrow::Cow;
use std::fs::File;
use std::io;
use std::io::{BufWriter, Write};
use std::process::Command;

pub struct RustGen;

#[derive(PartialEq, Copy, Clone)]
enum Ctx {
    Yes,
    No,
}

#[derive(Default)]
struct FuncArgs {
    func_args: Vec<String>,
    ffi_args: Vec<String>,
    body: String,
    ret_value: String,
}

fn get_arg_line(args: &[String]) -> String {
    let mut output = String::with_capacity(256);
    for (i, a) in args.iter().enumerate() {
        if i > 0 {
            output.push_str(", ")
        }

        output.push_str(a);
    }

    output
}

fn run_rustfmt(filename: &str) {
    Command::new("rustfmt")
        .arg(filename)
        .output()
        .expect("failed to execute cargo fmt");
}

impl RustGen {
    fn write_commment<W: Write>(
        f: &mut W,
        comments: &Vec<String>,
        indent: usize,
    ) -> io::Result<()> {
        for c in comments {
            writeln!(f, "{:indent$}/// {}", "", c, indent = indent)?;
        }

        Ok(())
    }

    /// Generate enums in the style of
    ///
    /// #[repr(C)]
    /// pub enum <EnumName> {
    ///    // Optional comment
    ///    <EntryName> = <Value>,
    /// }
    ///
    fn generate_enum<W: Write>(f: &mut W, enum_def: &Enum) -> io::Result<()> {
        Self::write_commment(f, &enum_def.doc_comments, 0)?;

        writeln!(
            f,
            "#[repr(C)]\n#[derive(Debug)]\npub enum {} {{",
            enum_def.name
        )?;

        for entry in &enum_def.entries {
            Self::write_commment(f, &entry.doc_comments, 4)?;
            match &entry.value {
                EnumValue::Value(v) => writeln!(f, "    {} = {},", entry.name, v)?,
                EnumValue::OrList(_) => panic!("OrList non-bitflags enums"),
                _ => (),
            }
        }

        writeln!(f, "}}\n")
    }

    fn generate_bitflags<W: Write>(f: &mut W, enum_def: &Enum) -> io::Result<()> {
        Self::write_commment(f, &enum_def.doc_comments, 0)?;

        writeln!(
            f,
            "bitflags! {{\n#[repr(C)]\npub struct {} : u32 {{",
            enum_def.name
        )?;

        for entry in &enum_def.entries {
            Self::write_commment(f, &entry.doc_comments, 4)?;
            match &entry.value {
                EnumValue::Value(v) => writeln!(
                    f,
                    "    const {} = {};",
                    entry.name.to_shouty_snake_case(),
                    v
                )?,
                EnumValue::OrList(v) => {
                    write!(f, "    const {} = ", entry.name.to_shouty_snake_case())?;
                    for (i, v) in v.iter().enumerate() {
                        if i > 0 {
                            write!(f, " | ")?;
                        }

                        write!(f, "Self::{}.bits()", v.to_shouty_snake_case())?;
                    }

                    writeln!(f, ";")?;
                }

                _ => (),
            }
        }

        writeln!(f, "}}}}\n")
    }

    pub fn get_primitive_type(var: &Variable) -> Cow<str> {
        let tname = var.type_name.as_str();

        match tname {
            "void" => "core::ffi::c_void".into(),
            _ => tname.into(),
        }
    }

    fn get_ffi_type(
        var: &Variable,
        self_type: &str,
        _type_name: &str,
        handle_struct: bool,
    ) -> String {
        if var.is_handle_type {
            return "u64".to_owned();
        }

        let mut output = String::with_capacity(256);

        match var.vtype {
            VariableType::None => output.push_str("core::ffi::c_void"),
            VariableType::SelfType => {
                if handle_struct {
                    output.push_str(self_type);
                } else {
                    output.push_str(&format!("*mut {}", self_type));
                }
            }
            VariableType::Reference => panic!("Shouldn't be here"),
            VariableType::Regular => output.push_str(&var.type_name),
            VariableType::Enum => output.push_str(&var.type_name),
            VariableType::Str => output.push_str("FlString"),
            VariableType::Primitive => output.push_str(&Self::get_primitive_type(var)),
        }

        match var.array.as_ref() {
            None => {
                if var.pointer {
                    format!("*mut {}", output)
                } else if var.const_pointer {
                    format!("*const {}", output)
                } else {
                    output
                }
            }

            Some(ArrayType::Unsized) => {
                format!("*const {}, {}_size: u32", output, var.name)
            }

            Some(ArrayType::SizedArray(size)) => {
                format!("[{}; {}]", output, size)
            }
        }
    }

    fn get_ffi_args(
        fa: &mut FuncArgs,
        func: &Function,
        self_name: &str,
        handle_struct: bool,
        with_ctx: Ctx,
    ) {
        for (i, arg) in func.function_args.iter().enumerate() {
            if i == 0 && func.func_type == FunctionType::Static {
                if with_ctx == Ctx::Yes {
                    fa.ffi_args
                        .push("data: *const core::ffi::c_void".to_owned());
                }
                continue;
            }

            if arg.name == "self" {
                fa.ffi_args.push(format!(
                    "self_c: {}",
                    Self::get_ffi_type(arg, self_name, &arg.type_name, handle_struct)
                ));
            } else {
                fa.ffi_args.push(format!(
                    "{}: {}",
                    arg.name,
                    Self::get_ffi_type(arg, self_name, &arg.type_name, handle_struct)
                ));
            }
        }

        if let Some(ret) = &func.return_val {
            fa.ret_value = Self::get_ffi_type(ret, self_name, "", handle_struct);
        }
    }

    fn generate_ffi_function<W: Write>(
        f: &mut W,
        func: &Function,
        self_name: &str,
        handle_struct: bool,
        with_ctx: Ctx,
    ) -> io::Result<()> {
        let mut fa = FuncArgs::default();

        Self::get_ffi_args(&mut fa, func, self_name, handle_struct, with_ctx);

        // write arguments

        if fa.ret_value.is_empty() {
            writeln!(
                f,
                "    pub(crate) {}: unsafe extern \"C\" fn({}),",
                func.name,
                get_arg_line(&fa.ffi_args)
            )
        } else {
            writeln!(
                f,
                "    pub(crate) {}: unsafe extern \"C\" fn({}) -> {},",
                func.name,
                get_arg_line(&fa.ffi_args),
                fa.ret_value
            )
        }
    }

    fn generate_extern_ffi_function<W: Write>(
        f: &mut W,
        func: &Function,
        self_name: &str,
        handle_struct: bool,
        with_ctx: Ctx,
    ) -> io::Result<()> {
        let mut fa = FuncArgs::default();

        Self::get_ffi_args(&mut fa, func, self_name, handle_struct, with_ctx);

        // write arguments

        if fa.ret_value.is_empty() {
            writeln!(
                f,
                "    fn fl_{}_{}_impl({});",
                self_name.to_snake_case(),
                func.name,
                get_arg_line(&fa.ffi_args)
            )
        } else {
            writeln!(
                f,
                "    fn fl_{}_{}_impl({}) -> {};",
                self_name.to_snake_case(),
                func.name,
                get_arg_line(&fa.ffi_args),
                fa.ret_value
            )
        }
    }

    fn generate_ffi_functions<W: Write>(f: &mut W, api_def: &ApiDef) -> io::Result<()> {
        for s in &api_def.structs {
            if s.functions.is_empty() || s.has_attribute("NoContext") {
                continue;
            }

            writeln!(f, "#[repr(C)]")?;
            writeln!(f, "pub struct {}FfiApi {{", s.name)?;
            writeln!(f, "    pub(crate) data: *const core::ffi::c_void,")?;

            for func in s
                .functions
                .iter()
                .filter(|&func| func.func_type != FunctionType::Manual && func.name != s.name)
            {
                Self::generate_ffi_function(f, func, &s.name, false, Ctx::Yes)?;
            }

            writeln!(f, "}}\n")?;
        }

        Ok(())
    }

    fn generate_extern_ffi_functions<W: Write>(f: &mut W, api_def: &ApiDef) -> io::Result<()> {
        for s in &api_def.structs {
            if s.functions.is_empty() || s.has_attribute("NoContext") {
                continue;
            }

            writeln!(f, "#[cfg(any(feature = \"static\", feature = \"tundra\"))]")?;
            writeln!(f, "extern \"C\" {{")?;

            for func in s
                .functions
                .iter()
                .filter(|&func| func.func_type != FunctionType::Manual && func.name != s.name)
            {
                Self::generate_extern_ffi_function(f, func, &s.name, false, Ctx::Yes)?;
            }

            writeln!(f, "}}\n")?;

            writeln!(f, "#[no_mangle]")?; 
            writeln!(f, "pub static mut g_flowi_{}_api: *const {}FfiApi = std::ptr::null_mut();\n", s.name.to_snake_case(), s.name)?;
        }

        Ok(())
    }

    fn generate_struct<W: Write>(f: &mut W, sdef: &Struct) -> io::Result<()> {
        Self::write_commment(f, &sdef.doc_comments, 0)?;

        if sdef.has_attribute("Handle") {
            writeln!(f, "#[repr(C)]")?;
            writeln!(f, "#[derive(Debug, Copy, Clone)]")?;
            writeln!(f, "pub struct {} {{ pub handle: u64 }}\n", sdef.name)
        } else {
            writeln!(f, "#[repr(C)]")?;

            if sdef.has_attribute("Copy") {
                writeln!(f, "#[derive(Copy, Clone, Debug)]")?;
            } else {
                writeln!(f, "#[derive(Debug)]")?;
            }

            writeln!(f, "pub struct {} {{", sdef.name)?;

            // TODO: Some members we like want to be private.
            for var in &sdef.variables {
                Self::write_commment(f, &var.doc_comments, 4)?;
                writeln!(
                    f,
                    "    pub {}: {},",
                    var.name,
                    Self::get_ffi_type(var, &sdef.name, &var.type_name, false)
                )?;
            }

            // if we have no variables in the struct we generate a dummy one because otherwise
            // rust will complain about it not being FFI compatible
            if sdef.variables.is_empty() {
                writeln!(f, "_dummy: u32")?;
            }

            writeln!(f, "}}\n")
        }
    }

    fn get_type(fa: &mut FuncArgs, var: &Variable, type_name: &str) {
        if var.array.is_some() {
            let sized: Cow<str> = match var.array.as_ref() {
                None => "".into(),
                Some(ArrayType::Unsized) => "".into(),
                Some(ArrayType::SizedArray(size)) => format!("; {}", size).into(),
            };

            if var.pointer {
                fa.func_args
                    .push(format!("{}: &mut [{}{}]", var.name, type_name, sized));
                fa.ffi_args.push(format!("{}.as_mut_ptr()", var.name));
                fa.ffi_args.push(format!("{}.len() as _", var.name));
            } else {
                fa.func_args
                    .push(format!("{}: &[{}{}]", var.name, type_name, sized));
                fa.ffi_args.push(format!("{}.as_ptr()", var.name));
                fa.ffi_args.push(format!("{}.len() as _", var.name));
            }
        } else if var.const_pointer {
            fa.func_args.push(format!("{}: &{}", var.name, type_name));
            fa.ffi_args.push(format!("{} as _", var.name));
        } else if var.pointer {
            fa.func_args
                .push(format!("{}: &mut {}", var.name, type_name));
            fa.ffi_args.push(format!("{} as _", var.name));
        } else {
            fa.func_args.push(format!("{}: {}", var.name, type_name));
            if var.is_handle_type {
                fa.ffi_args.push(format!("{}.handle", var.name));
            } else {
                fa.ffi_args.push(var.name.to_string());
            }
        }
    }

    fn generate_func_impl(func: &Function, self_name: &str, with_ctx: Ctx) -> FuncArgs {
        let mut fa = FuncArgs::default();

        //fa.body.push_str("let _api = &*self.api;");
        fa.body.push_str(&format!("let _api = &*g_flowi_{}_api;", self_name.to_snake_case()));

        for (i, arg) in func.function_args.iter().enumerate() {
            // Static for rust means we that we are in a Ctx/Ui instance
            if i == 0 && func.func_type == FunctionType::Static {
                if with_ctx == Ctx::Yes {
                    //fa.func_args.push("&self".to_owned());
                    fa.ffi_args.push("_api.data".to_owned());
                    //fa.ffi_args.push("self.handle as _".to_owned());
                }
                continue;
            }

            match arg.vtype {
                VariableType::None => panic!("Should not be here"),
                VariableType::Reference => panic!("Shouldn't be here"),
                VariableType::SelfType => {
                    fa.func_args.push("&self".to_owned());
                    if arg.is_handle_type {
                        fa.ffi_args.push("self.handle".to_owned());
                    } else {
                        fa.ffi_args.push("_api.data".to_owned());
                    }
                }

                VariableType::Regular => Self::get_type(&mut fa, arg, &arg.type_name),
                VariableType::Enum => Self::get_type(&mut fa, arg, &arg.type_name),
                VariableType::Str => {
                    fa.func_args.push(format!("{}: &str", arg.name));
                    fa.ffi_args.push(format!("FlString::new({})", arg.name));
                }

                VariableType::Primitive => {
                    Self::get_type(&mut fa, arg, &Self::get_primitive_type(arg));
                }
            }
        }

        if let Some(ret_val) = func.return_val.as_ref() {
            let ret = Self::get_primitive_type(ret_val);

            if ret_val.optional {
                if ret_val.const_pointer {
                    fa.ret_value = format!("Result<&'a {}>", ret);
                } else if ret_val.pointer {
                    fa.ret_value = format!("Result<&'a mut {}>", ret);
                } else {
                    fa.ret_value = format!("Result<{}>", ret);
                }
            } else {
                fa.ret_value = format!("{}", ret);
            }
        }

        fa
    }

    fn generate_callback_function<W: Write>(
        f: &mut W,
        func: &Function,
        self_name: &str,
    ) -> io::Result<()> {
        let mut fa = FuncArgs::default();

        Self::get_ffi_args(&mut fa, func, self_name, false, Ctx::Yes);

        writeln!(f, "#[allow(dead_code)]")?;

        // write arguments

        if fa.ret_value.is_empty() {
            writeln!(
                f,
                "type {} = extern \"C\" fn ({});",
                func.name,
                get_arg_line(&fa.ffi_args)
            )
        } else {
            writeln!(
                f,
                "type {} = extern \"C\" fn ({}) -> {};",
                func.name,
                get_arg_line(&fa.ffi_args),
                fa.ret_value
            )
        }
    }

    fn generate_func<W: Write>(
        f: &mut W,
        func: &Function,
        self_name: &str,
        with_ctx: Ctx,
    ) -> io::Result<()> {
        Self::write_commment(f, &func.doc_comments, 0)?;
        let struct_name = self_name.to_snake_case();

        let func_args = Self::generate_func_impl(func, self_name, with_ctx);
        let self_name = "";

        if func_args.ret_value.is_empty() {
            writeln!(
                f,
                "pub fn {}{}({}) {{",
                self_name,
                func.name,
                get_arg_line(&func_args.func_args)
            )?;
        } else {
            let mut needs_lifetime = false;
            if let Some(ret_val) = func.return_val.as_ref() {
                if ret_val.const_pointer || ret_val.pointer {
                    needs_lifetime = true;
                }
            }

            if needs_lifetime {
                writeln!(
                    f,
                    "pub fn {}{}<'a>({}) -> {} {{",
                    self_name,
                    func.name,
                    get_arg_line(&func_args.func_args),
                    func_args.ret_value
                )?;
            } else {
                writeln!(
                    f,
                    "pub fn {}{}({}) -> {} {{",
                    self_name,
                    func.name,
                    get_arg_line(&func_args.func_args),
                    func_args.ret_value
                )?;
            }
        }

        writeln!(f, "unsafe {{ ")?;
        if !func_args.body.is_empty() {
            write!(f, "{}", &func_args.body)?;
        }

        let args = get_arg_line(&func_args.ffi_args);

        if let Some(ret_val) = func.return_val.as_ref() {
            writeln!(f, "#[cfg(any(feature = \"static\", feature = \"tundra\"))]")?;
            writeln!(f, "let ret_val = fl_{}_{}_impl({});", struct_name, func.name, args)?;

            writeln!(f, "#[cfg(any(feature = \"dynamic\", feature = \"plugin\"))]")?;
            writeln!(f, "let ret_val = (_api.{})({});", func.name, args)?;

            if ret_val.is_handle_type {
                if ret_val.optional {
                    writeln!(
                        f,
                        "if ret_val == 0 {{ Err(get_last_error()) }} else {{ Ok({} {{ handle: ret_val }}) }}",
                        ret_val.type_name
                    )?;
                } else {
                    writeln!(f, "{} {{ handle: ret_value }}", ret_val.type_name)?;
                }
            } else if ret_val.const_pointer {
                if ret_val.optional {
                    writeln!(
                        f,
                        "if ret_val.is_null() {{ Err(get_last_error()) }} else {{ Ok(&*ret_val) }}"
                    )?;
                } else {
                    writeln!(f, "ret_val.as_ref()")?;
                }
            } else if ret_val.pointer {
                if ret_val.optional {
                    writeln!(f, "if ret_val.is_null() {{ Err(get_last_error()) }} else {{ Ok(&mut *ret_val) }}")?;
                } else {
                    writeln!(f, "ret_val.as_mut()")?;
                }
            } else {
                writeln!(f, "ret_val")?;
            }
        } else {
            writeln!(f, "#[cfg(any(feature = \"static\", feature = \"tundra\"))]")?;
            writeln!(f, "fl_{}_{}_impl({});", struct_name, func.name, args)?;

            writeln!(f, "#[cfg(any(feature = \"dynamic\", feature = \"plugin\"))]")?;
            writeln!(f, "(_api.{})({});", func.name, args)?;
        }

        writeln!(f, "}}\n}}\n")
    }

    fn generate_struct_impl<W: Write>(f: &mut W, sdef: &Struct) -> io::Result<()> {
        if sdef.functions.is_empty() || sdef.has_attribute("NoContext") {
            return Ok(());
        }

        /*
        writeln!(f, "#[repr(C)]")?;
        writeln!(f, "pub struct {}Api {{", sdef.name)?;
        writeln!(f, "   pub api: *const {}FfiApi,", sdef.name)?;
        writeln!(f, "}}\n")?;
        */

        writeln!(f, "impl {} {{", sdef.name)?;

        for func in &sdef.functions {
            // If the name is the same as the struct name, then it is the constructor
            if func.name == sdef.name {
                continue;
            }

            Self::generate_func(f, func, &sdef.name, Ctx::Yes)?;
        }

        writeln!(f, "}}\n")
    }

    pub fn generate(path: &str, api_def: &ApiDef) -> io::Result<()> {
        let filename = format!("{}/{}.rs", path, api_def.base_filename);

        {
            let mut f = BufWriter::new(File::create(&filename)?);

            println!("    Rust file Generating {}", filename);

            writeln!(f, "{}", RUST_FILE_HEADER)?;
            writeln!(f, "#[allow(unused_imports)]")?;
            writeln!(
                f,
                "use crate::manual::{{Result, get_last_error, FlString, Color}};\n"
            )?;
            writeln!(f, "#[allow(unused_imports)]")?;
            writeln!(f, "use bitflags::bitflags;\n")?;

            for m in &api_def.mods {
                writeln!(f, "#[allow(unused_imports)]")?;
                writeln!(f, "use crate::{}::*;\n", m)?;
            }

            Self::generate_ffi_functions(&mut f, api_def)?;
            Self::generate_extern_ffi_functions(&mut f, api_def)?;

            if !api_def.callbacks.is_empty() {
                for func in &api_def.callbacks {
                    Self::generate_callback_function(&mut f, func, "")?;
                }
                writeln!(f)?;
            }

            for enum_def in &api_def.enums {
                if enum_def.enum_type == EnumType::Regular {
                    Self::generate_enum(&mut f, enum_def)?;
                } else {
                    Self::generate_bitflags(&mut f, enum_def)?;
                }
            }

            for sdef in &api_def.structs {
                Self::generate_struct(&mut f, sdef)?;
            }

            //for sdef in api_def.structs.iter().filter(|s| s.functions.is_empty()) {
            for sdef in api_def.structs.iter() {
                Self::generate_struct_impl(&mut f, sdef)?;
            }
        }

        run_rustfmt(&filename);

        Ok(())
    }

    pub fn generate_mod_files(path: &str, api_defs: &[ApiDef]) -> io::Result<()> {
        let flowi_mod = format!("{}/mod.rs", path);

        {
            println!("    Rust file mod: {}", flowi_mod);

            let mut f = BufWriter::new(File::create(&flowi_mod)?);

            writeln!(f, "{}", RUST_FILE_HEADER)?;
            writeln!(f, "use core::ffi::c_void;")?;

            for api_def in api_defs {
                let base_filename = &api_def.base_filename;
                writeln!(f, "pub mod {};", base_filename)?;
                writeln!(f, "pub use {}::*;", base_filename)?;
            }

            for api_def in api_defs {
                let base_filename = &api_def.base_filename;

                for s in &api_def.structs {
                    if !s.functions.is_empty() && !s.has_attribute("NoContext") {
                        writeln!(f, "use crate::{}::{}FfiApi;", base_filename, s.name)?;
                        writeln!(f, "pub use crate::generated::{}::{} as {};", base_filename, s.name, s.name)?;
                    }
                }
            }

            writeln!(f)?;

            let structs_with_funcs = get_structs_with_functions(api_defs);

            writeln!(f, "#[repr(C)]")?;
            writeln!(f, "pub(crate) struct AppFfi {{")?;
            writeln!(f, "    pub(crate) data: *const c_void,")?;
            writeln!(f, "    pub(crate) main_loop: unsafe fn(data: *const c_void, user_data: *mut c_void) -> bool,")?;

            for s in &structs_with_funcs {
                let name = &s.name;
                writeln!(f, "   pub(crate) {}_get_api: unsafe extern \"C\" fn(data: *const c_void, api_ver: u32) -> *const {}FfiApi,",
                name.to_lowercase(), name)?;
            }

            writeln!(f, "}}\n")?;
            writeln!(f, "pub(crate) fn init_function_ptrs(api: *const AppFfi) {{")?;
            writeln!(f, "    unsafe {{")?;
            writeln!(f, "    let api = &*api;")?;

            for s in &structs_with_funcs {
                let name = s.name.to_snake_case();
                writeln!(
                    f,
                    "   g_flowi_{}_api = (api.{}_get_api)(api.data, 0);",
                    name,
                    name,
                )?;
            }

            writeln!(f, "}}}}\n")?;
        }

        run_rustfmt(&flowi_mod);

        Ok(())
    }
}

static RUST_FILE_HEADER: &str = "
// This file is auto-generated by api_gen. DO NOT EDIT!
";
