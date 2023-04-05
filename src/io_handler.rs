use base64ct::{Base64, Encoding};
use notify::{Config, RecommendedWatcher, RecursiveMode, Watcher};
use sha2::{Digest, Sha256};

use std::{
    fs::File,
    io::Read,
    path::{Path, PathBuf},
    process::{Command, Output},
    sync::mpsc::Receiver,
};

use crate::{
    internal_error::InternalResult as Result, io::IoFfiApi, manual::FlString,
    shaders::ShaderHandler, ShaderProgram,
};

pub struct IoHandler {
    watcher: RecommendedWatcher,
    event_rx: Receiver<notify::Result<notify::Event>>,
    pub shaders: ShaderHandler,
    //shaders_comp: HashMap<String, Shader>,
    temp_dir: PathBuf,
}

#[cfg(target_os = "macos")]
fn build_shader(filename: &str, output_path: &PathBuf, shader_type: &str) -> Result<Output> {
    Ok(Command::new(
        "/Users/emoon/code/projects/flowi/bin/shaderc_macos",
    )
    .arg("-f")
    .arg(filename)
    .arg("-i")
    .arg("/Users/emoon/code/other/bgfx/bgfx/src")
    .arg("--type")
    .arg(shader_type)
    .arg("--platform")
    .arg("osx")
    .arg("-p")
    .arg("metal")
    .arg("-o")
    .arg(output_path)
    .output()
    .unwrap())
}

#[cfg(target_os = "linux")]
fn build_shader(filename: &str, output_path: &PathBuf, shader_type: &str) -> Result<Output> {
    Ok(Command::new(
        "/home/emoon/code/projects/flowi/bin/shaderc_linux",
    )
    .arg("-f")
    .arg(filename)
    .arg("-i")
    .arg("/home/emoon/code/other/bgfx/bgfx/src")
    .arg("--type")
    .arg(shader_type)
    .arg("--platform")
    .arg("linux")
    .arg("-p")
    .arg("120")
    .arg("-o")
    .arg(output_path)
    .output()
    .unwrap())
}

fn build_fragment_shader(filename: &str, output_path: &PathBuf) -> Result<Output> {
    build_shader(filename, output_path, "fragment")
}

fn build_vertex_shader(filename: &str, output_path: &PathBuf) -> Result<Output> {
    build_shader(filename, output_path, "vertex")
}

impl IoHandler {
    pub fn new() -> Self {
        let (tx, event_rx) = std::sync::mpsc::channel();
        let watcher = RecommendedWatcher::new(tx, Config::default()).unwrap();
        Self {
            watcher,
            event_rx,
            temp_dir: std::env::temp_dir(),
            shaders: ShaderHandler::new(),
            //shaders_comp: HashMap::new(),
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

    pub fn load_fragment_shader_comp(&mut self, filename: &str) -> Result<Vec<u8>> {
        let mut file = File::open(filename)?;
        let mut hasher = Sha256::new();
        let _n = std::io::copy(&mut file, &mut hasher)?;
        let hash = hasher.finalize();
        let hash_str = Base64::encode_string(&hash);

        let temp_path = self.temp_dir.join(hash_str);
        // if the shader doesn't exist in the temp dir we need to generate it
        if !temp_path.exists() {
            let output = build_fragment_shader(filename, &temp_path).unwrap();   

            if !output.status.success() {
                println!("{}", String::from_utf8_lossy(&output.stdout));
                println!("{}", String::from_utf8_lossy(&output.stderr));
                panic!();
            }
        }

        self.watcher
            .watch(Path::new(filename), RecursiveMode::Recursive)?;

        let mut f = File::open(temp_path)?;
        let mut buffer = Vec::new();
        f.read_to_end(&mut buffer)?;

        // zero terminate the buffer as this seems to be required sometimes
        buffer.push(0);
        Ok(buffer)
    }

    /// TODO: Unify with code above
    pub fn load_vertex_shader_comp(&mut self, filename: &str) -> Result<Vec<u8>> {
        let mut file = File::open(filename)?;
        let mut hasher = Sha256::new();
        let _n = std::io::copy(&mut file, &mut hasher)?;
        let hash = hasher.finalize();
        let hash_str = Base64::encode_string(&hash);

        let temp_path = self.temp_dir.join(hash_str);
        // if the shader doesn't exist in the temp dir we need to generate it
        if !temp_path.exists() {
            let output = build_vertex_shader(filename, &temp_path).unwrap();

            println!("vertext shader at: {:?}", temp_path);

            if !output.status.success() {
                println!("{}", String::from_utf8_lossy(&output.stdout));
                println!("{}", String::from_utf8_lossy(&output.stderr));
                panic!();
            }
        }

        self.watcher
            .watch(Path::new(filename), RecursiveMode::Recursive)?;

        let mut f = File::open(temp_path)?;
        let mut buffer = Vec::new();
        f.read_to_end(&mut buffer)?;

        // zero terminate the buffer as this seems to be required sometimes
        buffer.push(0);
        Ok(buffer)
    }

    pub fn load_shader_program_comp(&mut self, vs: &str, fs: &str) -> Result<ShaderProgram> {
        let vs_data = self.load_vertex_shader_comp(vs)?;
        let fs_data = self.load_fragment_shader_comp(fs)?;
        self.shaders.load_program(&vs_data, &fs_data)
    }

    pub fn get_ffi_api(&self) -> IoFfiApi {
        IoFfiApi {
            data: self as *const IoHandler as *const std::ffi::c_void,
            load_shader_program_comp: fl_load_shader_program_comp,
        }
    }
}

#[no_mangle]
pub extern "C" fn fl_load_shader_program_comp(
    ctx: *const core::ffi::c_void,
    vs_filename: FlString,
    fs_filename: FlString,
) -> u64 {
    let io_handler = unsafe { &mut *(ctx as *mut IoHandler) };
    // TODO: correct handling
    let shader_handle = io_handler
        .load_shader_program_comp(vs_filename.as_str(), fs_filename.as_str())
        .unwrap();
    shader_handle.handle
}

#[no_mangle]
pub extern "C" fn fl_get_io_api(app_state: *const core::ffi::c_void, version: u32) -> *const IoFfiApi { 
    let app_state = unsafe { &*(app_state as *const crate::application::ApplicationState) };
    &app_state.io_ffi_api
}
