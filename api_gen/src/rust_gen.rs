use crate::api_parser::*;
use crate::c_gen::C_API_SUFFIX;
use heck::ToSnakeCase;
use std::borrow::Cow;
///
///
use std::fs::File;
use std::io;
use std::io::{BufWriter, Write};

pub struct RustGen;

#[derive(Default)]
struct FuncArgs {
    func_args: Vec<String>,
    ffi_args: Vec<String>,
    body: String,
    ret_value: String,
    post_body: String,
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
    fn write_commment<W: Write>(f: &mut W, comment: &str, indent: usize) -> io::Result<()> {
        if !comment.is_empty() {
            writeln!(f, "{:indent$}/// {}", "", comment, indent = indent)?;
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

        writeln!(f, "#[repr(C)]\npub enum {} {{", enum_def.name)?;

        for entry in &enum_def.entries {
            Self::write_commment(f, &entry.doc_comments, 4)?;
            writeln!(f, "    {} = {},", &entry.name, entry.value)?;
        }

        writeln!(f, "}}\n")
    }

    pub fn get_primitive_type(var: &Variable) -> Cow<str> {
        let tname = var.type_name.as_str();

        match tname {
            "void" => "c_void".into(),
            _ => tname.into(),
        }
    }

    fn get_ffi_type(var: &Variable, self_type: &str) -> String {
        let mut output = String::with_capacity(256);

        match var.vtype {
            VariableType::None => output.push_str("c_void"),
            VariableType::SelfType => output.push_str(&format!("*mut {}", self_type)),
            VariableType::Reference => panic!("Shouldn't be here"),
            VariableType::Regular => output.push_str(&format!("{}", var.type_name)),
            VariableType::Str => output.push_str("FlString"),
            VariableType::Primitive => output.push_str(&Self::get_primitive_type(&var)),
        }

        if var.pointer {
            format!("*mut {}", output)
        } else if var.const_pointer {
            format!("*const {}", output)
        } else {
            output
        }
    }

    fn generate_ffi_function<W: Write>(
        f: &mut W,
        func: &Function,
        self_name: &str,
    ) -> io::Result<()> {
        let mut function_args = String::with_capacity(128);
        let len = func.function_args.len();

        // write arguments
        for (i, arg) in func.function_args.iter().enumerate() {
            if i == 0 && func.func_type == FunctionType::Static {
                continue;
            }

            if arg.name == "self" {
                function_args.push_str("self_c");
            } else {
                function_args.push_str(&arg.name);
            }

            function_args.push_str(": ");
            function_args.push_str(&Self::get_ffi_type(&arg, self_name));

            if i != len - 1 {
                function_args.push_str(", ");
            }
        }

        write!(f, "    fn {}({})", func.c_name, function_args)?;

        if let Some(ret) = &func.return_val {
            writeln!(f, " -> {};", Self::get_ffi_type(&ret, self_name))
        } else {
            writeln!(f, ";")
        }
    }

    fn generate_ffi_functions<W: Write>(f: &mut W, api_def: &ApiDef) -> io::Result<()> {
        writeln!(f, "extern \"C\" {{")?;

        for s in &api_def.structs {
            for func in &s.functions {
                Self::generate_ffi_function(f, &func, &s.name)?;
            }
        }

        writeln!(f, "}}\n")
    }

    fn generate_struct<W: Write>(f: &mut W, sdef: &Struct) -> io::Result<()> {
        Self::write_commment(f, &sdef.doc_comments, 0)?;

        writeln!(f, "#[repr(C)]")?;
        writeln!(f, "pub struct {} {{", sdef.name)?;

        for var in &sdef.variables {
            Self::write_commment(f, &var.doc_comments, 4)?;
            writeln!(
                f,
                "    {}: {},",
                var.name,
                Self::get_ffi_type(var, &sdef.name)
            )?;
        }

        writeln!(f, "}}\n")
    }

    fn get_type(fa: &mut FuncArgs, var: &Variable, type_name: &str) {
        if var.array {
            if var.const_pointer {
                fa.func_args.push(format!("{}: &[{}]", var.name, type_name));
                fa.ffi_args.push(format!("{}.as_ptr()", var.name));
            } else if var.pointer {
                fa.func_args.push(format!("{}: &mut [{}]", var.name, type_name));
                fa.ffi_args.push(format!("{}.as_mut_ptr()", var.name));
            } else {
                panic!("Unsupported");
            }
        } else {
            if var.const_pointer {
                fa.func_args.push(format!("{}: &{}", var.name, type_name));
                fa.ffi_args.push(format!("{} as _", var.name));
            } else if var.pointer {
                fa.func_args.push(format!("{}: &mut {}", var.name, type_name));
                fa.ffi_args.push(format!("{} as _", var.name));
            } else {
                fa.func_args.push(format!("{}: {}", var.name, type_name));
                fa.ffi_args.push(format!("{}", var.name));
            }
        }
    }

    fn generate_func_impl(func: &Function) -> FuncArgs {
        let mut fa = FuncArgs::default();

        for (i, arg) in func.function_args.iter().enumerate() {
            if i == 0 && func.func_type == FunctionType::Static {
                continue;
            }

            match arg.vtype {
                VariableType::None => panic!("Should not be here"),
                VariableType::Reference => panic!("Shouldn't be here"),
                VariableType::SelfType => {
                    fa.func_args.push("&self".to_owned());
                    fa.body.push_str("let self_ = std::mem::transmute(self);\n");
                    fa.ffi_args.push("self_".to_owned());
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

        fa
    }

    fn generate_struct_impl<W: Write>(f: &mut W, sdef: &Struct) -> io::Result<()> {
        writeln!(f, "impl {} {{", sdef.name)?;

        for func in &sdef.functions {
            let func_args = Self::generate_func_impl(&func);

            //if func_args.ret_value.is_empty() {
            writeln!(
                f,
                "pub fn {}({}) {{",
                func.name,
                get_arg_line(&func_args.func_args)
            )?;

            writeln!(f, "unsafe {{ ")?;
            if !func_args.body.is_empty() {
                write!(f, "{}", &func_args.body)?;
            }

            let args = get_arg_line(&func_args.ffi_args);
            writeln!(f, "{}({});", func.c_name, args)?;

            writeln!(f, "}}\n}}\n")?;
        }

        writeln!(f, "}}\n")
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
