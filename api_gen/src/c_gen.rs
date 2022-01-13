use crate::api_parser::*;
///
///
use std::fs::File;
use std::io;
use std::io::{BufWriter, Write};

///
/// Base header for all header files
///
static HEADER: &str = "
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once\n
#include <stdint.h>
#include <stdbool.h>\n

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

static C_API_SUFIX_STRUCTS_ENUMS: &str = "Fl";
//static C_API_SUFIX_FUNCS: &str = "fl";

pub struct Cgen;

impl Cgen {
    fn write_commment<W: Write>(f: &mut W, comment: &str, indent: usize) -> io::Result<()> {
        if !comment.is_empty() {
            writeln!(f, "{:indent$}// {}", "", comment, indent=indent)?;
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

        writeln!(
            f,
            "typedef enum {}{} {{",
            C_API_SUFIX_STRUCTS_ENUMS, enum_def.name
        )?;

        for entry in &enum_def.entries {
            Self::write_commment(f, &entry.doc_comments, 4)?;

            writeln!(
                f,
                "    {}{}_{} = {},",
                C_API_SUFIX_STRUCTS_ENUMS, enum_def.name, &entry.name, entry.value
            )?;
        }

        writeln!(f, "}} {}{};\n", C_API_SUFIX_STRUCTS_ENUMS, enum_def.name)
    }

    /// Output variable for for struct
    fn write_struct_variable<W: Write>(f: &mut W, var: &Variable) -> io::Result<()> {
        Self::write_commment(f, &var.doc_comments, 4)?;

        match var.vtype {
            VariableType::None => panic!("Shouldn't be here"),
            VariableType::SelfType => panic!("Shouldn't be here"),
            //VariableType::Enum => panic!("Shouldn't be here"),
            VariableType::Reference => panic!("Shouldn't be here"),
            VariableType::Regular => write!(f, "    {}", var.type_name)?,
            VariableType::Str => write!(f, "    const char*")?,
            VariableType::Primitive => write!(f, "    {}", var.get_c_primitive_type())?,
        }

        if var.pointer {
            write!(f, "*")?;
        }

        // for arrays we generate a pointer and a size
        if var.array {
            writeln!(f, "* {};", var.name)?;
            writeln!(f, "    u32 {}_size;", var.name)
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

        writeln!(
            f,
            "typedef struct {}{} {{",
            C_API_SUFIX_STRUCTS_ENUMS, sdef.name
        )?;

        for var in &sdef.variables {
            Self::write_struct_variable(f, var)?;
        }

        writeln!(f, "}} {}{};\n", C_API_SUFIX_STRUCTS_ENUMS, sdef.name)
    }

    pub fn generate(filename: &str, api_def: &ApiDef) -> io::Result<()> {
        let mut f = BufWriter::new(File::create(filename)?);
        writeln!(f, "{}", HEADER)?;

        let mut render_commands = Vec::with_capacity(api_def.pod_structs.len());

        for enum_def in &api_def.enums {
            Self::generate_enum(&mut f, enum_def)?;
        }

        for sdef in &api_def.pod_structs {
            if sdef.has_attribute("RenderCommand") {
                render_commands.push(&sdef.name);
            }

            Self::generate_struct(&mut f, sdef)?;
        }

        // Generate the render commands enum
        if !render_commands.is_empty() {
            writeln!(f, "// Commands that will be in the render stream")?;
            writeln!(f, "typedef enum FlRenderCommand {{")?;
            for cmd in render_commands {
                writeln!(f, "    FlRenderCommand_{},", &cmd)?;
            }
            writeln!(f, "}} FlRenderCommand;\n")?;
        }

        writeln!(f, "{}", FOOTER)
    }
}
