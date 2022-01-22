use crate::api_parser::*;
use heck::ToSnakeCase;
///
///
use std::fs::File;
use std::io;
use std::io::{BufWriter, Write};

pub struct RustGen;

impl RustGen {
    fn write_commment<W: Write>(f: &mut W, comment: &str, indent: usize) -> io::Result<()> {
        if !comment.is_empty() {
            writeln!(f, "{:indent$}/// {}", "", comment, indent = indent)?;
        }

        Ok(())
    }

    /// Generate enums in the style of
    ///
    /// #[repr(c)]
    /// pub enum <EnumName> {
    ///    // Optional comment
    ///    <EntryName> = <Value>,
    /// }
    ///
    fn generate_enum<W: Write>(f: &mut W, enum_def: &Enum) -> io::Result<()> {
        Self::write_commment(f, &enum_def.doc_comments, 0)?;

        writeln!(f, "#[repr(c)]\npub enum {} {{", enum_def.name)?;

        for entry in &enum_def.entries {
            Self::write_commment(f, &entry.doc_comments, 4)?;
            writeln!( f, "    {} = {},", &entry.name, entry.value)?;
        }

        writeln!(f, "}}\n")?;
    }

}
