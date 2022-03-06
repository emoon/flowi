use crate::api_parser::*;
use heck::{ToSnakeCase, ToUpperCamelCase};
///
///
use std::fs::File;
use std::io;
use std::io::{BufWriter, Write};

static RENDER_CMD_HEADER: &str = "
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <flowi_core/render_commands.h>
#include \"command_buffer.h\"
";

///
/// Base header for all header files
///
static HEADER: &str = "
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once\n
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include \"idx.h\"
#include \"context.h\"
#include \"manual.h\"";

static HEADER2: &str = "

#ifdef __cplusplus
extern \"C\" {
#endif\n";

///
/// Footer that is generated at the end of the the file
///
static FOOTER: &str = "
#ifdef __cplusplus
}
#endif\n";

pub static C_API_SUFFIX: &str = "Fl";
pub static C_API_SUFIX_FUNCS: &str = "fl";

pub struct Cgen;

#[derive(PartialEq, Copy, Clone)]
enum Ctx {
    Yes,
    No,
}

#[derive(Default)]
struct FuncArgs {
    func_args: Vec<String>,
    internal_args: Vec<String>,
    call_args: Vec<String>,
    body: String,
    return_value: String,
}

fn arg_line(args: &[String], with_context: Ctx) -> String {
    let mut output = String::with_capacity(256);

    if with_context == Ctx::Yes {
        output.push_str("struct FlContext* ctx");
    }

    for (i, a) in args.iter().enumerate() {
        if i > 0 || with_context == Ctx::Yes {
            output.push_str(", ")
        }

        output.push_str(a);
    }

    output
}

fn get_args_name(func: &Function) -> String {
    format!("{}{}Args", C_API_SUFFIX, func.name.to_upper_camel_case())
}

impl Cgen {
    fn write_commment<W: Write>(
        f: &mut W,
        comments: &Vec<String>,
        indent: usize,
    ) -> io::Result<()> {
        if !comments.is_empty() {
            for c in comments {
                writeln!(f, "{:indent$}// {}", "", c, indent = indent)?;
            }
        }

        Ok(())
    }

    /// Generate enums in the style of
    ///
    /// typedef enum Fl<EnumName> {
    ///    // Optional comment
    ///    Fl<EnumName>_<EntryName> = <Value>,
    /// } Fl<EnumName>
    ///
    fn generate_enum<W: Write>(f: &mut W, enum_def: &Enum) -> io::Result<()> {
        Self::write_commment(f, &enum_def.doc_comments, 0)?;

        writeln!(f, "typedef enum {}{} {{", C_API_SUFFIX, enum_def.name)?;

        for entry in &enum_def.entries {
            Self::write_commment(f, &entry.doc_comments, 4)?;

            writeln!(
                f,
                "    {}{}_{} = {},",
                C_API_SUFFIX, enum_def.name, &entry.name, entry.value
            )?;
        }

        writeln!(f, "}} {}{};\n", C_API_SUFFIX, enum_def.name)
    }

    fn get_variable(var: &Variable, self_type: &str) -> String {
        let mut output = String::with_capacity(256);

        match var.vtype {
            VariableType::None => output.push_str("void"),
            VariableType::SelfType => {
                // hack
                if self_type == "Context*" {
                    output.push_str(&format!("struct {}{}", C_API_SUFFIX, self_type));
                } else {
                    output.push_str(&format!("{}{}", C_API_SUFFIX, self_type));
                }
            }

            VariableType::Reference => panic!("Shouldn't be here"),
            VariableType::Regular => {
                // hack, fix me
                if var.type_name == "Context" {
                    output.push_str("struct FlContext");
                } else if var.pointer && var.is_empty_struct {
                    output.push_str(&format!("struct {}{}", C_API_SUFFIX, var.type_name));
                } else {
                    output.push_str(&format!("{}{}", C_API_SUFFIX, var.type_name));
                }
            }

            VariableType::Enum => output.push_str(&format!("{}{}", C_API_SUFFIX, var.type_name)),
            VariableType::Str => output.push_str("FlString"),
            VariableType::Primitive => output.push_str(&var.get_c_primitive_type()),
        }

        if var.pointer {
            output.push('*');
        }

        output
    }

    /// Output variable for for struct
    fn write_struct_variable<W: Write>(f: &mut W, var: &Variable) -> io::Result<()> {
        Self::write_commment(f, &var.doc_comments, 4)?;

        write!(f, "    {}", Self::get_variable(var, ""))?;

        // for arrays we generate a pointer and a size
        match var.array {
            None => writeln!(f, " {};", var.name),
            Some(ArrayType::Unsized) => {
                writeln!(f, "* {};", var.name)?;
                writeln!(f, "    uint32_t {}_size;", var.name)
            }

            Some(ArrayType::SizedArray(ref size)) => {
                writeln!(f, " {}[{}];", var.name, size)
            }
        }
    }

    /// Generate structs in the style of
    ///
    /// typedef struct Fl<StructName> {
    ///    // Optional comment
    ///    ... entries
    /// } Fl<StructName>
    ///
    fn generate_struct<W: Write>(f: &mut W, sdef: &Struct) -> io::Result<()> {
        Self::write_commment(f, &sdef.doc_comments, 0)?;

        // if we have handle set we just generate it as a i32 instead
        if sdef.has_attribute("Handle") {
            writeln!(f, "typedef uint64_t {}{};\n", C_API_SUFFIX, sdef.name)
        } else if sdef.variables.is_empty() {
            writeln!(f, "struct {}{};", C_API_SUFFIX, sdef.name)
        } else {
            writeln!(f, "typedef struct {}{} {{", C_API_SUFFIX, sdef.name)?;

            for var in &sdef.variables {
                Self::write_struct_variable(f, var)?;
            }

            writeln!(f, "}} {}{};\n", C_API_SUFFIX, sdef.name)
        }
    }

    /// Generate structure and _default macro for functions that has default parameters
    fn generate_default_args_struct<W: Write>(
        f: &mut W,
        name: &str,
        args: &[&Variable],
    ) -> io::Result<()> {
        writeln!(f, "typedef struct {} {{", name)?;

        for var in args {
            Self::write_struct_variable(f, var)?;
        }

        writeln!(f, "}} {};\n", name)?;

        // Generate <StructName>_default macro for default value init of the struct such as
        // #define FoobarArgs_default (FoobarArgs){ .foo = 1, .bar = 0 }

        write!(f, "#define {}_default ({}){{", name, name)?;

        for (i, var) in args.iter().enumerate() {
            if i > 0 {
                write!(f, ",")?;
            }

            match var.vtype {
                VariableType::Enum => write!(
                    f,
                    " .{} = {}_{}",
                    var.name, var.type_name, var.default_value
                )?,
                _ => write!(f, " .{} = {}", var.name, var.default_value)?,
            }
        }

        writeln!(f, ")\n")
    }

    ///
    /// This is used for generating internal render file
    ///
    pub fn generate_render_file(filename: &str, render_commands: &[&String]) -> io::Result<()> {
        let mut f = BufWriter::new(File::create(filename)?);

        writeln!(f, "{}", RENDER_CMD_HEADER)?;

        // We want to generate in this sstyle
        // #define Render_create_texture_cmd(state) \
        //     (FlCreateTexture*)CommandBuffer_alloc_cmd(&state->render_commands, Fl_CreateTexture, sizeof(FlCreateTexture))

        for cmd in render_commands {
            let name = format!("{}{}", C_API_SUFFIX, cmd);
            writeln!(f, "#define Render_{}_cmd(state) \\", cmd.to_snake_case())?;
            writeln!(
                    f,
                    "    ({}*)CommandBuffer_alloc_cmd(&state->render_commands, {}RenderCommand_{}, sizeof({}))\n",
                    name, C_API_SUFFIX, cmd, name
                    )?;
        }

        Ok(())
    }

    fn generate_function_args(func: &Function, self_name: &str) -> FuncArgs {
        let mut fa = FuncArgs::default();

        fa.call_args.push("ctx".to_owned());

        for (i, arg) in func.function_args.iter().enumerate() {
            // skip first arg if type is manual or static
            if i == 0 && func.is_type_manual_static() {
                continue;
            }

            // If we have a default value we assemu the rest of of the args are default also
            if !arg.default_value.is_empty() {
                let args = format!("{} args", get_args_name(func));
                fa.func_args.push(args.to_owned());
                fa.internal_args.push(args.to_owned());
                fa.call_args.push("args".to_owned());
                break;
            }

            match arg.vtype {
                VariableType::Str => {
                    fa.func_args.push(format!("const char* {}", arg.name));
                    fa.body.push_str(&format!(
                        "FlString {}_ = fl_cstr_to_flstring({});",
                        arg.name, arg.name,
                    ));

                    fa.internal_args.push(format!("FlString {}", arg.name));
                    fa.call_args.push(format!("{}_", arg.name));
                }

                _ => match arg.array {
                    None => {
                        let carg = format!("{} {}", Self::get_variable(&arg, self_name), arg.name);
                        fa.func_args.push(carg.to_owned());
                        fa.internal_args.push(carg.to_owned());
                        fa.call_args.push(arg.name.to_owned());
                    }

                    Some(ArrayType::Unsized) => {
                        let carg = format!("{}* {}", Self::get_variable(&arg, self_name), arg.name);
                        fa.func_args.push(carg.to_owned());
                        fa.internal_args.push(carg.to_owned());
                        fa.call_args.push(arg.name.to_owned());
                        let carg = format!("uint32_t {}_size", arg.name);
                        fa.func_args.push(carg.to_owned());
                        fa.internal_args.push(carg.to_owned());
                        fa.call_args.push(format!("{}_size", arg.name));
                    }

                    Some(ArrayType::SizedArray(ref size)) => {
                        let carg = format!(
                            "{} {}[{}]",
                            Self::get_variable(&arg, self_name),
                            arg.name,
                            size
                        );
                        fa.func_args.push(carg.to_owned());
                        fa.internal_args.push(carg.to_owned());
                        fa.call_args.push(arg.name.to_owned());
                    }
                },
            }
        }

        if let Some(ret) = &func.return_val {
            if ret.const_pointer {
                fa.return_value = format!("{}*", Self::get_variable(&ret, self_name));
            } else {
                fa.return_value = Self::get_variable(&ret, self_name);
            }
        } else {
            fa.return_value = "void".to_owned();
        }

        fa
    }

    fn generate_callback_function<W: Write>(
        f: &mut W,
        func: &Function,
        self_name: &str,
    ) -> io::Result<()> {
        let fa = Self::generate_function_args(func, self_name);
        writeln!(
            f,
            "typedef {} (*{}{})({});",
            fa.return_value,
            C_API_SUFFIX,
            func.name,
            arg_line(&fa.func_args, Ctx::Yes)
        )
    }

    fn generate_function<W: Write>(
        f: &mut W,
        func: &Function,
        self_name: &str,
        with_ctx: Ctx,
    ) -> io::Result<()> {
        let fa = Self::generate_function_args(func, self_name);

        Self::write_commment(f, &func.doc_comments, 0)?;

        // write the implementation func

        #[rustfmt::skip]
        writeln!(f, "{} {}({});\n", fa.return_value, func.c_name, arg_line(&fa.internal_args, with_ctx))?;

        // write the inline function
        // TODO: Generate the internal inside a separate header to make things cleaner

        #[rustfmt::skip]
        let func_name = format!("{}_{}_{}", C_API_SUFIX_FUNCS, self_name.to_snake_case(), func.name);
        let func_name_c = &func_name;

        let arg_offset = if with_ctx == Ctx::Yes { 0 } else { 1 };

        writeln!(
            f,
            "FL_INLINE {} {}({}) {{",
            fa.return_value,
            func_name_c,
            arg_line(&fa.func_args, with_ctx)
        )?;

        if !fa.body.is_empty() {
            write!(f, "{}", &fa.body)?;
        }

        if fa.return_value != "void" {
            #[rustfmt::skip]
            writeln!(f, "return {}({});", func.c_name, arg_line(&fa.call_args[arg_offset..], Ctx::No))?;
        } else {
            #[rustfmt::skip]
            writeln!(f, "{}({});", func.c_name, arg_line(&fa.call_args[arg_offset..], Ctx::No))?;
        }

        writeln!(f, "}}\n")
    }

    pub fn generate(filename: &str, render_filename_dir: &str, api_def: &ApiDef) -> io::Result<()> {
        println!("    Generating Core C header: {}", filename);

        let mut f = BufWriter::new(File::create(filename)?);
        writeln!(f, "{}", HEADER)?;

        for m in &api_def.mods {
            writeln!(f, "#include \"{}.h\"", m)?;
        }

        writeln!(f, "{}", HEADER2)?;

        let mut render_commands = Vec::with_capacity(api_def.structs.len());

        for enum_def in &api_def.enums {
            Self::generate_enum(&mut f, enum_def)?;
        }

        for sdef in &api_def.structs {
            if sdef.has_attribute("RenderCommand") {
                render_commands.push(&sdef.name);
            }

            Self::generate_struct(&mut f, sdef)?;
        }

        // Generate the structs for default arges
        for sdef in &api_def.structs {
            for func in &sdef.functions {
                let default_args = func.get_default_args();

                if !default_args.is_empty() {
                    let name = get_args_name(func);
                    Self::generate_default_args_struct(&mut f, &name, &default_args)?;
                    let name = get_args_name(func);
                }
            }
        }

        // Generate callback defs

        if !api_def.callbacks.is_empty() {
            for func in &api_def.callbacks {
                Self::generate_callback_function(&mut f, &func, "")?;
            }
            writeln!(f)?;
        }

        for sdef in &api_def.structs {
            for func in &sdef.functions {
                let with_ctx = if sdef.has_attribute("NoContext") {
                    Ctx::No
                } else {
                    Ctx::Yes
                };
                if sdef.has_attribute("Handle") {
                    Self::generate_function(&mut f, &func, &sdef.name, with_ctx)?;
                } else {
                    Self::generate_function(&mut f, &func, &format!("{}*", sdef.name), with_ctx)?;
                }
            }
        }

        // Generate the render commands enum
        if !render_commands.is_empty() {
            let render_filename = format!("{}/render.h", render_filename_dir);
            println!(
                "    Generating RenderCommands C header: {}",
                render_filename
            );

            writeln!(f, "// Commands that will be in the render stream")?;
            writeln!(f, "typedef enum FlRenderCommand {{")?;
            for cmd in &render_commands {
                writeln!(f, "    FlRenderCommand_{},", &cmd)?;
            }
            writeln!(f, "}} FlRenderCommand;\n")?;

            Self::generate_render_file(&render_filename, &render_commands)?;
        }

        writeln!(f, "{}", FOOTER)
    }
}
