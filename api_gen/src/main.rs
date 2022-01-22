extern crate argparse;
extern crate heck;
extern crate liquid;
extern crate pest;
extern crate rayon;
extern crate walkdir;

mod api_parser;

#[macro_use]
extern crate pest_derive;

// Code for C/Header generation
mod c_gen;
mod lints;

use crate::api_parser::{ApiDef, ApiParser};
use crate::c_gen::Cgen;
use rayon::prelude::*;
//use std::fs;
//use std::process::Command;
use std::sync::RwLock;
use walkdir::WalkDir;

//
// Function for creating a directory and just bail in case it already exists.
// If there is an error here this code will panic as these directories are required in order for
// this program to work correctly.
//
/*
fn create_dir(path: &str) {
    // dir already existits so just bail
    if let Ok(p) = fs::metadata(path) {
        if p.is_dir() {
            return;
        }
    }

    // This function is expected to succed now when we have checked that the directory exists
    fs::create_dir(path).unwrap();
}
*/

//
// Run Rustfmt on generated file
//
/*
fn run_rustfmt(filename: &str) {
    Command::new("rustfmt")
        .arg(filename)
        .output()
        .expect("failed to execute cargo fmt");
}
*/

//
// Run Rustfmt on generated file
//
/*
fn run_clang_format(filename: &str) {
    Command::new("clang-format")
        .arg("-style=file")
        .arg("-i")
        .arg(filename)
        .output()
        .expect("failed to execute cargo fmt");
}
*/

///
/// Main
///
fn main() {
    let wd = WalkDir::new("../api");
    // temporary set to one thread during debugging
	// rayon::ThreadPoolBuilder::new().num_threads(1).build_global().unwrap();

    // Dest directores for various langs
	let c_core_dest_dir = "../core/c/include";
	let c_flowi_dest_dir = "../flowi/c/include";

    let rust_core_dest = "../core/rust-core/src";
    let rust_flowi_dest = "../flowi/rust/src";

    // Used for generating internal headers
	let c_core_src_dir = "../core/src";

    //let rust_dest_dir = "../rute/src/auto";
    //let dest = "../rute/cpp/auto";

    // Create the output dirs before doing anything else
    //create_dir(dest);
    //create_dir(rust_dest_dir);

    // Collect all files that needs to be parsed
    let files = wd
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|e| e.path().metadata().unwrap().is_file())
        .collect::<Vec<_>>();

    let api_defs = RwLock::new(Vec::with_capacity(files.len()));

    // Pass 1: Parse all the files

    files.par_iter().for_each(|f| {
        let mut api_def = ApiDef::default();

        println!("Parsing file {:?}", f.path());
        ApiParser::parse_file(&f.path(), &mut api_def);

        // Insert the api_def for later usage
        {
            let mut data = api_defs.write().unwrap();
            data.push(api_def);
        }
    });

    // patch up some refs, sort by filename for second pass

    {
        let mut data = api_defs.write().unwrap();
        ApiParser::second_pass(&mut data);
        data.sort_by(|a, b| a.filename.cmp(&b.filename))
    }

    let api_defs_read = api_defs.read().unwrap();

    // Pass 2:
    // Generate all the code.

    api_defs_read.par_iter().enumerate().for_each(|(_index, api_def)| {
        let base_filename = &api_def.base_filename;

        let c_filename;
        let rust_filename;

        if api_def.filename.contains("core") {
            c_filename = format!("{}/{}.h", c_core_dest_dir, base_filename);
            rust_filename = format!("{}/{}.rs", rust_core_dest, base_filename);
        } else {
            c_filename = format!("{}/{}.h", c_flowi_dest_dir, base_filename);
            rust_filename = format!("{}/{}.rs", rust_flowi_dest , base_filename);
        }

        //let c_render_cmds_filenames = format!("{}/render.h", c_core_src_dir);

        // Generate C/C++ Header for FFI structs
        Cgen::generate(&c_filename, &c_core_src_dir, api_def).unwrap();

        // Rust Rustfmt on rust files
        //run_rustfmt(&rust_ffi_target);
        //run_rustfmt(&rust_target);
    });

    // All done!
    println!("Generation complete!");
}
