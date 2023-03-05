use crate::manual::FlString;
use crate::shader::Shader;
use base64ct::{Base64, Encoding};
use notify::{Config, EventHandler, RecommendedWatcher, RecursiveMode, Result, Watcher};
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::fs::File;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::mpsc::Receiver;

pub struct IoHandler {
    watcher: RecommendedWatcher,
    event_rx: Receiver<Result<notify::Event>>,
    shaders_comp: HashMap<String, Shader>,
    temp_dir: PathBuf,
}

impl IoHandler {
    pub fn new() -> Self {
        let (tx, event_rx) = std::sync::mpsc::channel();
        let mut watcher = RecommendedWatcher::new(tx, Config::default()).unwrap();
        Self {
            watcher,
            event_rx,
            temp_dir: std::env::temp_dir(),
            shaders_comp: HashMap::new(),
        }
    }

    pub fn update(&mut self) {
        let event = self.event_rx.try_recv();
        match event {
            Ok(event) => {
                println!("event: {:?}", event);
            }
            Err(_) => {}
        }
    }

    pub fn load_fragment_shader_comp(&mut self, filename: &str) -> Shader {
        // TODO: Clean this up

        let mut file = File::open(filename).unwrap();
        let mut hasher = Sha256::new();
        let n = std::io::copy(&mut file, &mut hasher).unwrap();
        let hash = hasher.finalize();
        let hash_str = Base64::encode_string(&hash);

        let temp_path = self.temp_dir.join(hash_str);
        // if the shader doesn't exist in the temp dir we need to generate it
        if !temp_path.exists() {
            // Command = "$(BGFX_SHADERC) $(COMPILE_PARAMS) -i core/c/external/bgfx/src -f $(<) --bin2c -o $(@)",
            // ShaderC { Source = src, OutName = dest .. "_glsl.h", Parameters = "--type fragment --platform linux -p 120" },
            // TODO: This should be called async
            let output = Command::new(
                "/home/emoon/code/other/bgfx/bgfx/.build/linux64_gcc/bin/shadercRelease",
            )
            .arg("-f")
            .arg(filename)
            .arg("-i")
            .arg("/home/emoon/code/other/bgfx/bgfx/src")
            .arg("--type")
            .arg("fragment")
            .arg("--platform")
            .arg("linux")
            .arg("-p")
            .arg("120")
            .arg("-o")
            .arg(&temp_path)
            .output()
            .expect("shaderc failed");

            if !output.status.success() {
                println!("{}", String::from_utf8_lossy(&output.stdout));
                println!("{}", String::from_utf8_lossy(&output.stderr));
                panic!();
            }
        }

        dbg!(temp_path);

        //self.shaders_comp
        //    .insert(filename.to_string(), Shader::new());

        self.watcher
            .watch(Path::new(filename), RecursiveMode::Recursive)
            .unwrap();

        Shader { handle: 1 }
    }
}

#[no_mangle]
pub extern "C" fn io_handler_create() -> *const IoHandler {
    dbg!("IoHandler::new");
    Box::into_raw(Box::new(IoHandler::new()))
}

#[no_mangle]
pub extern "C" fn io_handler_update(ctx: *const core::ffi::c_void) {
    dbg!("IoHandler::new");
    let io_handler = unsafe { &mut *(ctx as *mut IoHandler) };
    io_handler.update();
}

#[no_mangle]
pub extern "C" fn load_fragment_shader(
    ctx: *const core::ffi::c_void,
    filename: FlString,
) -> Shader {
    let io_handler = unsafe { &mut *(ctx as *mut IoHandler) };
    dbg!("load_fragment_shader");
    io_handler.load_fragment_shader_comp(filename.as_str())
}
