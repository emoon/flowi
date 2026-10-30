//! Regenerates flowi's committed codegen from the IDL.
//!
//! Calls api_gen's library API directly; there is no argv in between. Output is
//! deterministic, so re-running on an unchanged tree is a no-op diff. Run via
//! `cargo regen`; `cargo regen-check` asserts the committed output is current.

fn main() {
    let root = flowi_codegen::repo_root();
    for target in flowi_codegen::targets(&root, &root) {
        match api_gen::generate(&target.config) {
            Ok(summary) => println!(
                "api_gen: {}: {} module(s), {} exported symbol(s)",
                target.name, summary.modules, summary.exported_symbols
            ),
            Err(e) => {
                eprintln!("api_gen: {}: {e}", target.name);
                std::process::exit(1);
            }
        }
    }
}
