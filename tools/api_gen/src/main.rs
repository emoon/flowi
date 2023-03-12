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
use rayon::prelude::*;
use rust_gen::RustGen;
//use std::fs;
use std::sync::RwLock;
use walkdir::WalkDir;

/// Main
fn main() {
    let wd = WalkDir::new("../../api");
    // temporary set to one thread during debugging
    //rayon::ThreadPoolBuilder::new()
    // .num_threads(1)
    // .build_global()
    // .unwrap();

    // Dest directores for various langs
    let c_dest = "../../langs/c_cpp/include/flowi";
    let rust_dest = "../../src/generated";

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
        ApiParser::parse_file(f.path(), &mut api_def);

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

    // Pass 2: Generate all the code.
    api_defs_read
        .par_iter()
        .enumerate()
        .for_each(|(index, api_def)| {
            // On the first thread we start with generating a bunch of main files so we have this
            // generation running threaded as well. Next time when index isn't 0 anymore regular work
            // will come along here.

            if index == 0 {
                Cgen::generate_main_file(c_dest, &api_defs_read).unwrap();
                RustGen::generate_mod_files(rust_dest, &api_defs_read).unwrap();

                println!("Generating {}/flowi.h", c_dest);
            }

            // Generate C/C++ Header for FFI structs
            if let Err(e) = RustGen::generate(rust_dest, api_def) {
                println!("ERROR: Unable to write rust file, error: {:?}", e);
            }

            // Generate C/C++ Header for FFI structs
            if let Err(e) = Cgen::generate(c_dest, api_def) {
                panic!("ERROR: Unable to write, error: {:?}", e);
            }

        });

    // All done!
    println!("Generation complete! {}", api_defs_read.len());
}
