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

#include \"../include/render_commands.h\"
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
#include \"idx.h\"

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
enum IsCallback {
    Yes,
    No,
}

impl Cgen {
    fn write_commment<W: Write>(f: &mut W, comment: &str, indent: usize) -> io::Result<()> {
        if !comment.is_empty() {
            writeln!(f, "{:indent$}// {}", "", comment, indent = indent)?;
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
            VariableType::SelfType => output.push_str(&format!("{}{}*", C_API_SUFFIX, self_type)),
            VariableType::Reference => panic!("Shouldn't be here"),
            VariableType::Regular => output.push_str(&format!("{}{}", C_API_SUFFIX, var.type_name)),
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
        if var.array {
            writeln!(f, "* {};", var.name)?;
            writeln!(f, "    uint32_t {}_size;", var.name)
        } else {
            writeln!(f, " {};", var.name)
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

        writeln!(f, "typedef struct {}{} {{", C_API_SUFFIX, sdef.name)?;

        for var in &sdef.variables {
            Self::write_struct_variable(f, var)?;
        }

        writeln!(f, "}} {}{};\n", C_API_SUFFIX, sdef.name)
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

    fn generate_function<W: Write>(
        f: &mut W,
        func: &Function,
        self_name: &str,
        is_callback: IsCallback,
    ) -> io::Result<()> {
        let mut function_args = String::with_capacity(128);
        let len = func.function_args.len();

        // write arguments
        for (i, arg) in func.function_args.iter().enumerate() {
            if i == 0 && func.func_type == FunctionType::Static {
                continue;
            }

            function_args.push_str(&Self::get_variable(&arg, self_name));
            function_args.push_str(" ");
            function_args.push_str(&arg.name);

            if i != len - 1 {
                function_args.push_str(", ");
            }
        }

        let return_value;

        if let Some(ret) = &func.return_val {
            return_value = Self::get_variable(&ret, self_name);
        } else {
            return_value = "void".to_owned();
        }

        Self::write_commment(f, &func.doc_comments, 0)?;

        if is_callback == IsCallback::Yes {
            writeln!(
                f,
                "typedef {} (*{}{})({});",
                return_value, C_API_SUFFIX, func.name, function_args
            )
        } else {
            writeln!(f, "{} {}({});", return_value, func.c_name, function_args)
        }
    }

    pub fn generate(filename: &str, render_filename_dir: &str, api_def: &ApiDef) -> io::Result<()> {
        println!("    Generating Core C header: {}", filename);

        let mut f = BufWriter::new(File::create(filename)?);
        writeln!(f, "{}", HEADER)?;

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
                Self::generate_function(&mut f, &func, "", IsCallback::Yes)?;
            }
            writeln!(f, "")?;
        }

        for sdef in &api_def.structs {
            for func in &sdef.functions {
                Self::generate_function(&mut f, &func, &sdef.name, IsCallback::No)?;
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
