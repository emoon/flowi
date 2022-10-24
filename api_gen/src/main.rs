extern crate argparse;
extern crate heck;
extern crate pest;
extern crate rayon;
extern crate walkdir;

mod api_parser;

#[macro_use]
extern crate pest_derive;

mod c_gen;
mod lints;
mod rust_gen;

use crate::api_parser::{ApiDef, ApiParser};
use crate::c_gen::Cgen;
use crate::rust_gen::RustGen;
use rayon::prelude::*;
//use std::fs;
use std::process::Command;
use std::sync::RwLock;
use walkdir::WalkDir;
use crate::c_gen::DynamicOutput;

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

fn run_rustfmt(filename: &str) {
    Command::new("rustfmt")
        .arg(filename)
        .output()
        .expect("failed to execute cargo fmt");
}

//
// Run Rustfmt on generated file
//
fn run_clang_format(filename: &str) {
    Command::new("clang-format")
        .arg("-style=file")
        .arg("-i")
        .arg(filename)
        .output()
        .expect("failed to execute clang-format");
}

///
/// Main
///
fn main() {
    let wd = WalkDir::new("../api");
    // temporary set to one thread during debugging
    //rayon::ThreadPoolBuilder::new()
    // .num_threads(1)
    // .build_global()
    // .unwrap();

    // Dest directores for various langs
    let c_core_dest_static_dir = "../core/c/include/static/flowi_core";
    let c_core_dest_dynamic_dir = "../core/c/include/dynamic/flowi_core";

    let c_flowi_dest_static_dir = "../flowi/c/include/static/flowi";
    let c_flowi_dest_dynamic_dir = "../flowi/c/include/dynamic/flowi";

    let rust_core_dest = "../core/rust/flowi-core/src";
    let rust_flowi_dest = "../flowi/rust/flowi/src";

    // Used for generating internal headers
    let c_core_src_dir = "../core/c/src";

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

    api_defs_read
        .par_iter()
        .enumerate()
        .for_each(|(index, api_def)| {
            let base_filename = &api_def.base_filename;

            let c_static_path;
            let c_dynamic_path;
            let rust_filename;

            // On the first thread we start with generating a bunch of main files so we have this
            // generation running threaded as well. Next time when index isn't 0 anymore regular work
            // will come along here.

            if index == 0 {
                RustGen::generate_mod_files(rust_core_dest, rust_flowi_dest, &api_defs_read)
                    .unwrap();
            }

            if api_def.filename.contains("core") {
                c_static_path = c_core_dest_static_dir ;
                c_dynamic_path = c_core_dest_dynamic_dir;
                //c_filename = format!("{}/{}.h", c_core_dest_dir, base_filename);
                //inl_filename = format!("{}/{}.inl", c_core_dest_dir, base_filename);
                rust_filename = format!("{}/{}.rs", rust_core_dest, base_filename);
            } else {
                c_static_path = c_core_dest_static_dir ;
                c_dynamic_path = c_core_dest_dynamic_dir;
                //c_filename = format!("{}/{}.h", c_flowi_dest_dir, base_filename);
                //inl_filename = format!("{}/{}.inl", c_flowi_dest_dir, base_filename);
                rust_filename = format!("{}/{}.rs", rust_flowi_dest, base_filename);
            }

            // Generate C/C++ Header for FFI structs
            if let Err(e) = Cgen::generate(DynamicOutput::No, &c_static_path, &api_def) {
                println!("ERROR: Unable to write, error: {:?}", e);
            }

            // Generate C/C++ Header for FFI structs
            if let Err(e) = Cgen::generate(DynamicOutput::Yes, &c_dynamic_path, &api_def) {
                println!("ERROR: Unable to write, error: {:?}", e);
            }

            // Generate C/C++ Header for FFI structs
            if let Err(e) = RustGen::generate(&rust_filename, api_def) {
                println!("ERROR: Unable to write {}, error: {:?}", rust_filename, e);
            }

            // Rust Rustfmt on rust files
            run_rustfmt(&rust_filename);

            // clang-format C files
            //run_clang_format(&c_filename);

            // clang-format C files
            //run_clang_format(&inl_filename);
        });

    // All done!
    println!("Generation complete!");
}
