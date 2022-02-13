use crate::api_parser::*;
use heck::ToSnakeCase;
use std::borrow::Cow;
///
///
use std::fs::File;
use std::io;
use std::io::{BufWriter, Write};

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
            writeln!(f, "    {} = {},", &entry.name, entry.value)?;
        }

        writeln!(f, "}}\n")
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
        type_name: &str,
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
            VariableType::Regular => output.push_str(&format!("{}", var.type_name)),
            VariableType::Str => output.push_str("FlString"),
            VariableType::Primitive => output.push_str(&Self::get_primitive_type(&var)),
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

    fn generate_ffi_function<W: Write>(
        f: &mut W,
        func: &Function,
        self_name: &str,
        handle_struct: bool,
        with_ctx: Ctx,
    ) -> io::Result<()> {
        let mut function_args = String::with_capacity(128);
        let len = func.function_args.len();

        // write arguments
        for (i, arg) in func.function_args.iter().enumerate() {
            if i == 0 && func.func_type == FunctionType::Static {
                if with_ctx == Ctx::Yes {
                    function_args.push_str("ctx: *const core::ffi::c_void");
                    if len > 0 {
                        function_args.push_str(", ");
                    }
                }
                continue;
            }

            if arg.name == "self" {
                function_args.push_str("self_c");
            } else {
                function_args.push_str(&arg.name);
            }

            function_args.push_str(": ");
            function_args.push_str(&Self::get_ffi_type(
                &arg,
                self_name,
                &arg.name,
                handle_struct,
            ));

            if i != len - 1 {
                function_args.push_str(", ");
            }
        }

        write!(f, "    fn {}({})", func.c_name, function_args)?;

        if let Some(ret) = &func.return_val {
            writeln!(
                f,
                " -> {};",
                Self::get_ffi_type(&ret, self_name, "", handle_struct)
            )
        } else {
            writeln!(f, ";")
        }
    }

    fn generate_ffi_functions<W: Write>(f: &mut W, api_def: &ApiDef) -> io::Result<()> {
        writeln!(f, "extern \"C\" {{")?;

        for s in &api_def.structs {
            let handle_struct = s.has_attribute("Handle");
            let with_ctx = if s.has_attribute("NoContext") {
                Ctx::No
            } else {
                Ctx::Yes
            };
            for func in &s.functions {
                Self::generate_ffi_function(f, &func, &s.name, handle_struct, with_ctx)?;
            }
        }

        writeln!(f, "}}\n")
    }

    fn generate_struct<W: Write>(f: &mut W, sdef: &Struct) -> io::Result<()> {
        Self::write_commment(f, &sdef.doc_comments, 0)?;

        if sdef.has_attribute("Handle") {
            writeln!(f, "#[repr(C)]")?;
            writeln!(f, "#[derive(Debug)]")?;
            writeln!(f, "pub struct {} {{ pub handle: u64 }}\n", sdef.name)
        } else {
            writeln!(f, "#[repr(C)]")?;
            writeln!(f, "#[derive(Debug)]")?;
            writeln!(f, "pub struct {} {{", sdef.name)?;

            for var in &sdef.variables {
                Self::write_commment(f, &var.doc_comments, 4)?;
                writeln!(
                    f,
                    "    {}: {},",
                    var.name,
                    Self::get_ffi_type(var, &sdef.name, &var.type_name, false)
                )?;
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
        } else {
            if var.const_pointer {
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
                    fa.ffi_args.push(format!("{}", var.name));
                }
            }
        }
    }

    fn generate_func_impl(func: &Function, with_ctx: Ctx) -> FuncArgs {
        let mut fa = FuncArgs::default();

        for (i, arg) in func.function_args.iter().enumerate() {
            // Static for rust means we that we are in a Ctx/Ui instance
            if i == 0 && func.func_type == FunctionType::Static {
                if with_ctx == Ctx::Yes {
                    fa.func_args.push("&self".to_owned());
                    fa.body.push_str("let self_ = std::mem::transmute(self);\n");
                    fa.ffi_args.push("self_".to_owned());
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
                        fa.body.push_str("let self_ = std::mem::transmute(self);\n");
                        fa.ffi_args.push("self_".to_owned());
                    }
                }

                VariableType::Regular => Self::get_type(&mut fa, &arg, &arg.type_name),
                VariableType::Str => {
                    fa.func_args.push(format!("{}: &str", arg.name));
                    fa.ffi_args.push(format!("FlString::new({})", arg.name));
                }

                VariableType::Primitive => {
                    Self::get_type(&mut fa, &arg, &Self::get_primitive_type(&arg));
                }
            }
        }

        if let Some(ret_val) = func.return_val.as_ref() {
            let ret = Self::get_primitive_type(&ret_val);

            if ret_val.optional {
                if ret_val.const_pointer {
                    fa.ret_value = format!("Result<&{}>", ret);
                } else if ret_val.pointer {
                    fa.ret_value = format!("Result<&mut {}>", ret);
                } else {
                    fa.ret_value = format!("Result<{}>", ret);
                }
            } else {
                fa.ret_value = format!("{}", ret);
            }
        }

        fa
    }

    fn generate_func<W: Write>(
        f: &mut W,
        func: &Function,
        self_name: &str,
        with_ctx: Ctx,
    ) -> io::Result<()> {
        Self::write_commment(f, &func.doc_comments, 0)?;

        let func_args = Self::generate_func_impl(&func, with_ctx);

        if func_args.ret_value.is_empty() {
            writeln!(
                f,
                "pub fn {}{}({}) {{",
                self_name,
                func.name,
                get_arg_line(&func_args.func_args)
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

        writeln!(f, "unsafe {{ ")?;
        if !func_args.body.is_empty() {
            write!(f, "{}", &func_args.body)?;
        }

        let args = get_arg_line(&func_args.ffi_args);

        if let Some(ret_val) = func.return_val.as_ref() {
            writeln!(f, "let ret_val = {}({});", func.c_name, args)?;

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
            } else {
                if ret_val.const_pointer {
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
            }
        } else {
            writeln!(f, "{}({});", func.c_name, args)?;
        }

        writeln!(f, "}}\n}}\n")
    }

    fn generate_struct_impl<W: Write>(f: &mut W, sdef: &Struct) -> io::Result<()> {
        if sdef.functions.is_empty() {
            return Ok(());
        }

        if sdef
            .functions
            .iter()
            .find(|&func| func.func_type == FunctionType::Static)
            .is_some()
        {
            writeln!(f, "impl Context {{")?;

            let self_name = format!("{}_", sdef.name.to_snake_case());
            let with_ctx = if sdef.has_attribute("NoContext") {
                Ctx::No
            } else {
                Ctx::Yes
            };

            for func in sdef
                .functions
                .iter()
                .filter(|func| func.func_type == FunctionType::Static && with_ctx == Ctx::Yes)
            {
                Self::generate_func(f, &func, &self_name, Ctx::Yes)?;
            }

            writeln!(f, "}}\n")?;
        }

        if sdef
            .functions
            .iter()
            .find(|&func| func.func_type != FunctionType::Static || sdef.has_attribute("NoContext"))
            .is_some()
        {
            writeln!(f, "impl {} {{", sdef.name)?;
            let with_ctx = if sdef.has_attribute("NoContext") {
                Ctx::No
            } else {
                Ctx::Yes
            };

            for func in sdef.functions.iter().filter(|func| {
                (func.func_type != FunctionType::Static || with_ctx == Ctx::No)
                    && func.func_type != FunctionType::Manual
            }) {
                Self::generate_func(f, &func, "", with_ctx)?;
            }

            writeln!(f, "}}\n")?;
        }

        Ok(())
    }

    pub fn generate(filename: &str, api_def: &ApiDef) -> io::Result<()> {
        let mut f = BufWriter::new(File::create(filename)?);

        println!("    Rust file Generating {}", filename);

        writeln!(f, "{}", RUST_FILE_HEADER)?;
        writeln!(f, "#[allow(unused_imports)]")?;
        writeln!(f, "use crate::*;\n")?;

        if !filename.contains("core") {
            writeln!(f, "#[allow(unused_imports)]")?;
            writeln!(f, "use flowi_core::*;\n")?;
        }

        Self::generate_ffi_functions(&mut f, api_def)?;

        for enum_def in &api_def.enums {
            Self::generate_enum(&mut f, enum_def)?;
        }

        for sdef in &api_def.structs {
            Self::generate_struct(&mut f, sdef)?;
        }

        //for sdef in api_def.structs.iter().filter(|s| s.functions.is_empty()) {
        for sdef in api_def.structs.iter() {
            Self::generate_struct_impl(&mut f, sdef)?;
        }

        Ok(())
    }

    pub fn generate_mod_files(
        core_dir: &str,
        flowi_dir: &str,
        api_defs: &[ApiDef],
    ) -> io::Result<()> {
        let core_mod = format!("{}/lib.rs", core_dir);
        let flowi_mod = format!("{}/lib.rs", flowi_dir);

        println!("    Rust file mod: {}", core_mod);
        println!("    Rust file mod: {}", flowi_mod);

        let mut core_mod = BufWriter::new(File::create(core_mod)?);
        let mut flowi_mod = BufWriter::new(File::create(flowi_mod)?);

        writeln!(core_mod, "{}", RUST_FILE_HEADER)?;
        writeln!(flowi_mod, "{}", RUST_FILE_HEADER)?;

        for api_def in api_defs {
            let base_filename = &api_def.base_filename;
            let file_target;

            if api_def.filename.contains("core") {
                file_target = &mut core_mod;
            } else {
                file_target = &mut flowi_mod;
            }

            writeln!(file_target, "pub mod {};", base_filename)?;
            writeln!(file_target, "pub use {}::*;\n", base_filename)?;
        }

        // manual is used implementing things that aren't auto-generated

        writeln!(core_mod, "pub mod manual;")?;
        writeln!(core_mod, "pub use manual::*;\n")?;
        writeln!(flowi_mod, "pub mod manual;")?;
        writeln!(flowi_mod, "pub use manual::*;\n")
    }
}

static RUST_FILE_HEADER: &str = "
// This file is auto-generated by api_gen. DO NOT EDIT!
";
