use crate::api_parser::*;
use heck::ToSnakeCase;
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
#include \"manual.h\"";

static HEADER2: &str = "

struct FlContext;

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

#[derive(PartialEq)]
enum WithContext {
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

fn get_arg_line(args: &[String], with_context: WithContext) -> String {
    let mut output = String::with_capacity(256);

    if with_context == WithContext::Yes {
        output.push_str("struct FlContext* flowi_ctx");
    }

    for (i, a) in args.iter().enumerate() {
        if i > 0 || with_context == WithContext::Yes {
            output.push_str(", ")
        }

        output.push_str(a);
    }

    output
}

impl Cgen {
    fn write_commment<W: Write>(f: &mut W, comments: &Vec<String>, indent: usize) -> io::Result<()> {
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
            VariableType::SelfType => output.push_str(&format!("{}{}", C_API_SUFFIX, self_type)),
            VariableType::Reference => panic!("Shouldn't be here"),
            VariableType::Regular => {
                // hack, fix me
                if var.type_name == "Context" {
                    output.push_str("struct FlContext");
                } else {
                    output.push_str(&format!("{}{}", C_API_SUFFIX, var.type_name));
                }
            },

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
        } else {
            writeln!(f, "typedef struct {}{} {{", C_API_SUFFIX, sdef.name)?;

            for var in &sdef.variables {
                Self::write_struct_variable(f, var)?;
            }

            writeln!(f, "}} {}{};\n", C_API_SUFFIX, sdef.name)
        }
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
    /*
    func_args: Vec<String>,
    internal_args: Vec<String>,
    call_args: Vec<String>,
    body: String,
    */

    fn generate_function_args(func: &Function, self_name: &str) -> FuncArgs {
        let mut fa = FuncArgs::default();

        fa.call_args.push("flowi_ctx".to_owned());

        for (i, arg) in func.function_args.iter().enumerate() {
            if i == 0 && func.func_type == FunctionType::Static {
                continue;
            }

            match arg.vtype {
                VariableType::Str => {
                    fa.func_args.push(format!("const char* {}", arg.name));
                    fa.body.push_str(&format!(
                        "FlString {}_ = {{ {}, 1, (uint32_t)strlen({}) }};",
                        arg.name, arg.name, arg.name
                    ));

                    fa.internal_args.push(format!("FlString {}", arg.name));
                    fa.call_args.push(format!("{}_", arg.name));
                }

                _ => {
                    // TODO: support arrays with fixed size
                    let carg = format!("{} {}", Self::get_variable(&arg, self_name), arg.name);
                    fa.func_args.push(carg.to_owned());
                    fa.internal_args.push(carg.to_owned());
                    fa.call_args.push(arg.name.to_owned());
                }
            }

            // If we have an array we add name with size after the parameter
            match arg.array {
                None => (),
                Some(ArrayType::Unsized) => {
                    let carg = format!("uint32_t {}_size", arg.name);
                    fa.func_args.push(carg.to_owned());
                    fa.internal_args.push(carg.to_owned());
                    fa.call_args.push(format!("{}_size", arg.name));
                }

                Some(ArrayType::SizedArray(ref _size)) => {
                    unimplemented!();
                }
            }
        }

        if let Some(ret) = &func.return_val {
            fa.return_value = Self::get_variable(&ret, self_name);
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
            get_arg_line(&fa.func_args, WithContext::Yes)
        )
    }

    fn generate_function<W: Write>(f: &mut W, func: &Function, self_name: &str) -> io::Result<()> {
        let fa = Self::generate_function_args(func, self_name);

        Self::write_commment(f, &func.doc_comments, 0)?;

        // write the implementation func

        writeln!(f, "{} {}({});\n", fa.return_value, func.c_name, get_arg_line(&fa.internal_args, WithContext::Yes))?;

        // write the inline function
        // TODO: Generate the internal inside a separate header to make things cleaner

        let func_name = format!("{}_{}_{}", C_API_SUFIX_FUNCS, self_name.to_snake_case(), func.name);
        let func_name_c = format!("{}_ctx", func_name);

        writeln!(f, "FL_INLINE {} {}({}) {{", fa.return_value, func_name_c, get_arg_line(&fa.func_args, WithContext::Yes))?;
        if !fa.body.is_empty() {
            write!(f, "{}", &fa.body)?;
        }

        if fa.return_value != "void" {
            writeln!(f, "return {}({});", func.c_name, get_arg_line(&fa.call_args, WithContext::No))?;
        } else {
            writeln!(f, "{}({});", func.c_name, get_arg_line(&fa.call_args, WithContext::No))?;
        }

        writeln!(f, "}}\n")?;

        writeln!(f, "#define {}({}) {}({})\n", func_name,
            get_arg_line(&fa.call_args[1..], WithContext::No),
            func_name_c,
            get_arg_line(&fa.call_args, WithContext::No))
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

        // Generate callback defs

        if !api_def.callbacks.is_empty() {
            for func in &api_def.callbacks {
                Self::generate_callback_function(&mut f, &func, "")?;
            }
            writeln!(f, "")?;
        }

        for sdef in &api_def.structs {
            for func in &sdef.functions {
                if sdef.has_attribute("Handle") {
                    Self::generate_function(&mut f, &func, &sdef.name)?;
                } else {
                    Self::generate_function(&mut f, &func, &format!("{}*", sdef.name))?;
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
