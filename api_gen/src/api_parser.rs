use heck::ToSnakeCase;
use pest::iterators::Pair;
use pest::Parser;
use std::borrow::Cow;
use std::collections::{HashMap, HashSet};
use std::fs::File;
use std::io::Read;
use std::path::Path;

//#[cfg(debug_assertions)]
const _GRAMMAR: &str = include_str!("api.pest");

///
/// Current primitive types
///
const PRMITIVE_TYPES: &[&str] = &[
    "void", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "bool", "f32", "f64",
];

///
/// Variable type
///
#[derive(PartialEq, Debug, Clone, Copy)]
pub enum VariableType {
    None,
    /// Self (aka this pointer in C++ and self in Rust)
    SelfType,
    /// Enum type
    Enum,
    /// Struct/other type
    Regular,
    /// String type
    Str,
    /// Prmitive type (such as i32,u64,etc)
    Primitive,
    /// Reference type
    Reference,
}

///
/// Array Type
///
#[derive(PartialEq, Debug, Clone)]
pub enum ArrayType {
    /// Array is unsized
    Unsized,
    /// Array with fixed size
    SizedArray(String),
}

impl Default for ArrayType {
    fn default() -> ArrayType {
        ArrayType::Unsized
    }
}

///
/// Holds the data for a variable. It's name and it's type and additional flags
///
#[derive(Debug, Clone)]
pub struct Variable {
    /// Documentation
    pub doc_comments: Vec<String>,
    /// Which def file this variable comes from
    pub def_file: String,
    /// Name of the variable
    pub name: String,
    /// Type of the variable
    pub vtype: VariableType,
    /// Name of the variable type
    pub type_name: String,
    /// Name of the variable type
    pub default_value: String,
    /// Type of enum
    pub enum_type: EnumType,
    /// If variable is an array
    pub array: Option<ArrayType>,
    /// If variable is optional (nullable)
    pub optional: bool,
    /// Type is a mutable pointer
    pub pointer: bool,
    /// Type is a const pointer
    pub const_pointer: bool,
    /// If the type is a Handle (attribute set on struct)
    pub is_handle_type: bool,
    /// If this type is a struct
    pub is_empty_struct: bool,
}

///
/// Default implementation for Variable
///
impl Default for Variable {
    fn default() -> Self {
        Variable {
            name: String::new(),
            doc_comments: Vec::new(),
            def_file: String::new(),
            vtype: VariableType::None,
            type_name: String::new(),
            enum_type: EnumType::Regular,
            default_value: String::new(),
            array: None,
            optional: false,
            pointer: false,
            const_pointer: false,
            is_handle_type: false,
            is_empty_struct: false,
        }
    }
}

///
/// Function type
///
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum FunctionType {
    /// This is a regular function in a Qt Class
    Regular,
    /// Static function is that doesn't belong to a class
    Static,
    /// Function that is manually implemented in some cases
    Manual,
}

///
/// Holds the data for a function. Name, function_args, return_type, etc
///
#[derive(Debug, Clone)]
pub struct Function {
    /// Documentation
    pub doc_comments: Vec<String>,
    /// Which def file this function comes from
    pub def_file: String,
    /// Name of the function
    pub name: String,
    /// Name of the C function
    pub c_name: String,
    /// Function argumnts
    pub function_args: Vec<Variable>,
    /// Return value
    pub return_val: Option<Variable>,
    /// Type of function. See FunctionType descrition for more info
    pub func_type: FunctionType,
}

///
/// Default implementation for Function
///
impl Default for Function {
    fn default() -> Self {
        Function {
            doc_comments: Vec::new(),
            name: String::new(),
            c_name: String::new(),
            def_file: String::new(),
            function_args: Vec::new(),
            return_val: None,
            func_type: FunctionType::Regular,
        }
    }
}

///
/// Holds the data for a struct
///
#[derive(Debug, Default)]
pub struct Struct {
    /// Docummentanion
    pub doc_comments: Vec<String>,
    /// Name
    pub name: String,
    /// Which def file this struct comes from
    pub def_file: String,
    /// Variables in the struct
    pub variables: Vec<Variable>,
    /// Functions for the struct
    pub functions: Vec<Function>,
    /// Attributes of thu struct
    pub attributes: Vec<String>,
    /// Traits
    pub traits: Vec<String>,
}

#[derive(Debug)]
pub enum EnumValue {
    /// Enum value with a value
    None,
    /// Enum value with a value
    Value(String),
    /// Values that or being or-ed together
    OrList(Vec<String>),
}

/// Enum
#[derive(Debug)]
pub struct EnumEntry {
    /// Documentation
    pub doc_comments: Vec<String>,
    /// Name of the enum entry
    pub name: String,
    /// Value of the enum entry
    pub value: EnumValue,
}

///
/// Enums in C++ can have same value for different enum ids. This isn't supported in Rust.
/// Also Rust doesn't support that your "or" enums flags so we need to handle that.
///
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum EnumType {
    /// All values are in sequantial order and no overlap
    Regular,
    /// This enum is constructed with bitflags due to being power of two or overlapping values
    Bitflags,
}

impl Default for EnumType {
    fn default() -> EnumType {
        EnumType::Regular
    }
}

///
/// Enum type
///
#[derive(Debug, Default)]
pub struct Enum {
    /// Documentation
    pub doc_comments: Vec<String>,
    /// Name of the enum
    pub name: String,
    /// The file this enum is present in
    pub def_file: String,
    /// Type of enum
    pub enum_type: EnumType,
    /// Qt supports having a flags macro on enums being type checked with an extra name
    pub flags_name: String,
    /// All the enem entries
    pub entries: Vec<EnumEntry>,
}

///
/// Api definition for a file
///
#[derive(Debug, Default)]
pub struct ApiDef {
    /// full filename path
    pub filename: String,
    /// Base filename (such as foo/file/some_name.def) is some_name
    pub base_filename: String,
    /// Mods to to be included in the file
    pub mods: Vec<String>,
    /// Callbacks types
    pub callbacks: Vec<Function>,
    /// Structs that only holds data
    pub structs: Vec<Struct>,
    /// Enums
    pub enums: Vec<Enum>,
}

///
/// Checks if name is a primitive
///
fn is_primitve(name: &str) -> bool {
    PRMITIVE_TYPES.iter().any(|&type_name| type_name == name)
}

#[derive(Parser)]
#[grammar = "api.pest"]
pub struct ApiParser;

///
/// Build struct info for a parsed API def file
///
impl ApiParser {
    pub fn parse_file<P: AsRef<Path>>(path: P, api_def: &mut ApiDef) {
        let mut buffer = String::new();

        let pathname = path.as_ref().to_str().unwrap();

        let mut f = File::open(pathname)
            .unwrap_or_else(|e| panic!("ApiParser: Unable to open {}: {}", pathname, e));
        f.read_to_string(&mut buffer)
            .unwrap_or_else(|e| panic!("ApiParser: Unable to read from {}: {}", pathname, e));

        Self::parse_string(&buffer, pathname, api_def);
    }

    pub fn parse_string(buffer: &str, filename: &str, api_def: &mut ApiDef) {
        let chunks = ApiParser::parse(Rule::chunk, buffer)
            .unwrap_or_else(|e| panic!("APiParser: {} {}", filename, e));

        let base_filename = Path::new(filename).file_name().unwrap().to_str().unwrap();
        let base_filename = &base_filename[..base_filename.len() - 4];
        let mut current_comments = Vec::new();

        api_def.filename = filename.to_owned();
        api_def.base_filename = base_filename.to_owned();

        for chunk in chunks {
            match chunk.as_rule() {
                Rule::structdef => {
                    let sdef = Self::fill_struct(chunk, &current_comments, &api_def.base_filename);
                    current_comments.clear();

                    // If we have some variables in the struct we push it to pod_struct
                    api_def.structs.push(sdef);
                }

                Rule::callbackdef => {
                    let mut func = Self::fill_callback(chunk, &current_comments);
                    func.func_type = FunctionType::Static;
                    api_def.callbacks.push(func);
                    current_comments.clear();
                }

                Rule::moddef => {
                    for entry in chunk.into_inner() {
                        if entry.as_rule() == Rule::name {
                            api_def.mods.push(entry.as_str().to_owned());
                        }
                    }
                }

                Rule::doc_comment => {
                    current_comments.push(chunk.as_str()[4..].to_owned());
                }

                Rule::enumdef => {
                    let mut enum_def = Enum {
                        def_file: base_filename.to_owned(),
                        doc_comments: current_comments.to_owned(),
                        ..Default::default()
                    };
                    current_comments.clear();

                    let mut attributes = Vec::new();

                    for entry in chunk.into_inner() {
                        match entry.as_rule() {
                            Rule::name => enum_def.name = entry.as_str().to_owned(),
                            Rule::attributes => attributes = Self::get_attrbutes(entry),
                            Rule::fieldlist => enum_def.entries = Self::fill_field_list_enum(entry),
                            _ => (),
                        }
                    }

                    // Figure out enum type
                    enum_def.enum_type = Self::determine_enum_type(&attributes);
                    api_def.enums.push(enum_def);
                }

                _ => (),
            }
        }
    }

    /// Figures out the type of enum
    fn determine_enum_type(attributes: &[String]) -> EnumType {
        if attributes.iter().any(|t| t == "bitflags") {
            EnumType::Bitflags
        } else {
            EnumType::Regular
        }
    }

    fn fill_callback(chunk: Pair<Rule>, doc_comments: &Vec<String>) -> Function {
        let mut func = Function::default();

        for entry in chunk.into_inner() {
            if entry.as_rule() == Rule::function {
                func = Self::get_function(entry, doc_comments);
            }
        }

        func
    }

    /// Fill struct def
    fn fill_struct(chunk: Pair<Rule>, doc_comments: &Vec<String>, def_file: &str) -> Struct {
        let mut sdef = Struct {
            doc_comments: doc_comments.to_owned(),
            def_file: def_file.to_owned(),
            ..Default::default()
        };

        for entry in chunk.into_inner() {
            match entry.as_rule() {
                Rule::name => sdef.name = entry.as_str().to_owned(),
                Rule::attributes => sdef.attributes = Self::get_attrbutes(entry),
                Rule::traits => sdef.traits = Self::get_attrbutes(entry),
                Rule::fieldlist => {
                    let (var_entries, func_entries) = Self::fill_field_list(entry);
                    sdef.variables = var_entries;
                    sdef.functions = func_entries;
                }

                _ => (),
            }
        }

        sdef
    }

    /// Get attributes for a struct
    fn get_attrbutes(rule: Pair<Rule>) -> Vec<String> {
        let mut attribs = Vec::new();
        for entry in rule.into_inner() {
            if entry.as_rule() == Rule::namelist {
                attribs = Self::get_namelist_list(entry);
            }
        }

        attribs
    }

    /// collect namelist (array) of strings
    fn get_namelist_list(rule: Pair<Rule>) -> Vec<String> {
        rule.into_inner().map(|e| e.as_str().to_owned()).collect()
    }

    /// Fill the entries in a struct
    /// Returns tuple with two ararys for variables and functions
    fn fill_field_list(rule: Pair<Rule>) -> (Vec<Variable>, Vec<Function>) {
        let mut var_entries = Vec::new();
        let mut func_entries = Vec::new();
        let mut doc_comments = Vec::new();

        for entry in rule.into_inner() {
            match entry.as_rule() {
                Rule::field => {
                    let field = entry.clone().into_inner().next().unwrap();

                    match field.as_rule() {
                        Rule::var => {
                            var_entries.push(Self::get_variable(field, &doc_comments));
                            doc_comments.clear();
                        }
                        Rule::function => {
                            func_entries.push(Self::get_function(field, &doc_comments));
                            doc_comments.clear();
                        }
                        _ => (),
                    }
                }

                Rule::doc_comment => {
                    doc_comments.push(entry.as_str()[4..].to_owned());
                }

                _ => (),
            }
        }

        (var_entries, func_entries)
    }

    ///
    /// Get data for function declaration
    ///
    fn get_function(rule: Pair<Rule>, doc_comments: &Vec<String>) -> Function {
        let mut function = Function {
            doc_comments: doc_comments.to_owned(),
            ..Function::default()
        };

        for entry in rule.into_inner() {
            match entry.as_rule() {
                Rule::name => function.name = entry.as_str().to_owned(),
                Rule::static_typ => function.func_type = FunctionType::Static,
                Rule::manual_typ => function.func_type = FunctionType::Manual,
                Rule::varlist => function.function_args = Self::get_variable_list(entry),
                Rule::retexp => function.return_val = Some(Self::get_variable(entry, &Vec::new())),
                _ => (),
            }
        }

        // if we don't have any function args we add self as first argument as we always have that
        if function.function_args.is_empty() {
            function.function_args.push(Variable {
                name: "self".to_owned(),
                vtype: VariableType::SelfType,
                type_name: "self".to_owned(),
                ..Variable::default()
            });
        }

        function
    }

    ///
    /// Gather variable list
    ///
    fn get_variable_list(rule: Pair<Rule>) -> Vec<Variable> {
        let mut variables = vec![Variable {
            name: "self".to_owned(),
            vtype: VariableType::SelfType,
            ..Variable::default()
        }];

        let t = Vec::new();

        for entry in rule.into_inner() {
            variables.push(Self::get_variable(entry, &t));
        }

        variables
    }

    fn get_default_value(var: &mut Variable, rule: Pair<Rule>) {
        let mut default_value = String::new();
        for entry in rule.into_inner() {
            match entry.as_rule() {
                Rule::name_or_num => {
                    default_value = entry.as_str().to_owned();
                    break;
                }

                Rule::string => {
                    default_value = entry.as_str().to_owned();
                    break;
                }
                _ => (),
            }
        }

        var.default_value = default_value;
    }

    ///
    /// Get variable
    ///
    fn get_variable(rule: Pair<Rule>, doc_comments: &Vec<String>) -> Variable {
        let mut vtype = Rule::var;
        let mut var = Variable::default();
        let mut type_name = String::new();

        var.doc_comments = doc_comments.to_owned();

        for entry in rule.into_inner() {
            match entry.as_rule() {
                Rule::name => var.name = entry.as_str().to_owned(),
                Rule::refexp => vtype = Rule::refexp,
                Rule::pointer_exp => vtype = Rule::pointer_exp,
                Rule::const_ptr_exp => vtype = Rule::const_ptr_exp,
                Rule::optional => var.optional = true,
                Rule::vtype => type_name = entry.as_str().to_owned(),
                Rule::default_val => Self::get_default_value(&mut var, entry),

                Rule::array => {
                    var.array = Some(ArrayType::Unsized);
                    // Get the type if we have an array
                    for entry in entry.into_inner() {
                        match entry.as_rule() {
                            Rule::vtype => type_name = entry.as_str().to_owned(),
                            Rule::refexp => vtype = Rule::refexp,
                            Rule::pointer_exp => vtype = Rule::pointer_exp,
                            Rule::const_ptr_exp => vtype = Rule::const_ptr_exp,
                            Rule::array_size => {
                                var.array = Some(ArrayType::SizedArray(
                                    entry.into_inner().as_str().to_owned(),
                                ));
                            }
                            _ => (),
                        }
                    }
                }

                _ => (),
            }
        }

        if !var.default_value.is_empty() {
            dbg!(&var.default_value);
        }

        // match up with the correct type
        let var_type = match vtype {
            Rule::refexp => VariableType::Reference,
            //Rule::pointer_exp => VariableType::Reference,
            _ => {
                if type_name == "String" {
                    VariableType::Str
                } else if is_primitve(&type_name) {
                    VariableType::Primitive
                } else {
                    VariableType::Regular
                }
            }
        };

        if vtype == Rule::pointer_exp {
            var.pointer = true;
        }

        if vtype == Rule::const_ptr_exp {
            var.const_pointer = true;
        }

        var.type_name = type_name;
        var.vtype = var_type;
        var
    }

    ///
    /// Get array of enums
    ///
    fn fill_field_list_enum(rule: Pair<Rule>) -> Vec<EnumEntry> {
        let mut entries = Vec::new();
        let mut doc_comments = Vec::new();

        for entry in rule.into_inner() {
            match entry.as_rule() {
                Rule::field => {
                    let field = entry.clone().into_inner().next().unwrap();

                    if field.as_rule() == Rule::enum_type {
                        entries.push(Self::get_enum(&doc_comments, field));
                        doc_comments.clear();
                    }
                }

                Rule::doc_comment => {
                    doc_comments.push(entry.as_str()[4..].to_owned());
                }

                _ => (),
            }
        }

        entries
    }

    /// Get enum
    fn get_enum(doc_comments: &Vec<String>, rule: Pair<Rule>) -> EnumEntry {
        let mut name = String::new();
        let mut value = EnumValue::None;

        for entry in rule.into_inner() {
            match entry.as_rule() {
                Rule::name => name = entry.as_str().to_owned(),
                Rule::enum_assign => value = Self::get_enum_assign(entry),
                _ => (),
            }
        }

        EnumEntry {
            doc_comments: doc_comments.to_owned(),
            name,
            value,
        }
    }

    /// Get enum assign
    fn get_enum_assign(rule: Pair<Rule>) -> EnumValue {
        let mut strings = Vec::new();

        for entry in rule.into_inner() {
            if entry.as_rule() == Rule::or_namelist {
                for e in entry.into_inner() {
                    if e.as_rule() == Rule::string_to_end {
                        strings.push(e.as_str().trim().to_owned());
                    }
                }
            }
        }

        if strings.len() == 1 {
            EnumValue::Value(strings[0].to_owned())
        } else {
            EnumValue::OrList(strings)
        }
    }

    pub fn second_pass(api_defs: &mut [ApiDef]) {
        // TODO: Investigate if we actually need this pass
        // Build a hash map of all type and their types
        // and we also build two hashmaps for all types and which modules they belong into
        // and they are separate for structs and enums
        let mut type_def_file = HashMap::new();
        let mut enum_def_file_type = HashMap::new();
        let mut empty_structs = HashSet::new();

        for api_def in api_defs.iter() {
            api_def.structs.iter().for_each(|s| {
                if s.variables.is_empty() && !s.has_attribute("Handle") {
                    empty_structs.insert(s.name.to_owned());
                }
                type_def_file.insert(s.name.to_owned(), s.def_file.to_owned());
                type_def_file.insert(format!("{}Trait", s.name), s.def_file.to_owned());
            });

            api_def.enums.iter().for_each(|e| {
                enum_def_file_type.insert(e.name.to_owned(), (e.def_file.to_owned(), e.enum_type));

                if !e.flags_name.is_empty() {
                    enum_def_file_type.insert(
                        e.flags_name.to_owned(),
                        (e.def_file.to_owned(), EnumType::Bitflags),
                    );
                }
            });
        }

        for api_def in api_defs.iter_mut() {
            for s in &mut api_def.structs {
                for func in &mut s.functions {
                    func.c_name = format!(
                        "{}_{}_{}",
                        crate::c_gen::C_API_SUFIX_FUNCS,
                        s.name.to_snake_case(),
                        func.name
                    );

                    for arg in &mut func.function_args {
                        if enum_def_file_type.contains_key(&arg.type_name) {
                            arg.vtype = VariableType::Enum;
                        }
                    }
                }
            }
        }

        // Patch up so handle types are marked as such
        for api_def in api_defs.iter_mut() {
            for s in &mut api_def.structs {
                let is_handle_type = s.has_attribute("Handle");
                for func in &mut s.functions {
                    for arg in &mut func.function_args {
                        if arg.type_name == s.name {
                            arg.is_handle_type = is_handle_type;
                        }

                        if empty_structs.contains(&arg.type_name) {
                            arg.is_empty_struct = true;
                        }
                    }

                    if let Some(ret_var) = func.return_val.as_mut() {
                        if ret_var.type_name == s.name {
                            ret_var.is_handle_type = is_handle_type;
                        }

                        if empty_structs.contains(&ret_var.type_name) {
                            ret_var.is_empty_struct = true;
                        }
                    }
                }
            }
        }
    }
}

///
/// Impl for struct. Mostly helper functions to make it easier to extract info
///
impl Struct {
    ///
    /// Check if no wrapping class should be generated
    ///
    pub fn has_attribute(&self, attrib: &str) -> bool {
        self.attributes.iter().any(|s| s == attrib)
    }
}

/// Helper functions for funtctions
impl Function {
    pub fn get_default_args(&self) -> Vec<&Variable> {
        self.function_args
            .iter()
            .filter(|arg| !arg.default_value.is_empty())
            .collect()
    }

    pub fn is_type_manual_static(&self) -> bool {
        self.func_type == FunctionType::Static || self.func_type == FunctionType::Manual
    }
}

/// Impl for Variable. Helper functions to make C and Rust generation easier
impl Variable {
    pub fn get_c_primitive_type(&self) -> Cow<str> {
        let tname = self.type_name.as_str();

        match tname {
            "f32" => "float".into(),
            "bool" => "bool".into(),
            "f64" => "double".into(),
            "i32" => "int".into(),
            "void" => "void".into(),
            _ => {
                if self.type_name.starts_with('u') {
                    format!("uint{}_t", &tname[1..]).into()
                } else {
                    format!("int{}_t", &tname[1..]).into()
                }
            }
        }
    }
}

pub fn get_structs_with_functions(api_defs: &[ApiDef]) -> Vec<&Struct> {
    let mut data: Vec<&Struct> = api_defs
        .iter()
        .flat_map(|api_def| api_def.structs.iter())
        .filter(|s| !s.functions.is_empty() && !s.has_attribute("NoContext"))
        .collect();

    data.sort_by(|a, b| a.name.cmp(&b.name));
    data
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_primitve_ok() {
        assert!(is_primitve("i32"));
    }

    #[test]
    fn test_primitve_false() {
        assert!(is_primitve("dummy"));
    }

    ///
    /// Make sure parsing of "struct Widget { show() }"
    ///
    #[test]
    fn test_basic_class_struct() {
        let mut api_def = ApiDef::default();
        ApiParser::parse_string(
            "struct Widget { show() }",
            "dummy_filename.def",
            &mut api_def,
        );
        assert!(api_def.structs.is_empty());
        assert!(api_def.structs.is_empty());

        let sdef = &api_def.structs[0];

        assert_eq!(sdef.name, "Widget");
        assert_eq!(sdef.functions.len(), 1);
        assert_eq!(sdef.functions[0].name, "show");
    }
}
