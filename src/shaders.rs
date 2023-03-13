use std::collections::HashMap;
use crate::internal_error::InternalResult as Result;
use crate::internal_error::InternalError;
use crate::ShaderProgram;
use bgfx_rs::bgfx;
use bgfx::*;

pub struct ShaderHandler {
    programs: HashMap<u64, bgfx::Program>,
    shader_id: u64,
}

impl ShaderHandler {
    pub fn new () -> Self {
        Self {
            programs: HashMap::new(),
            shader_id: 1,
        }
    }

    pub fn load_program(&mut self, vs: &[u8], fs: &[u8]) -> Result<ShaderProgram> {
        let vs_data = Memory::copy(&vs);
        let fs_data = Memory::copy(&fs);

        let vs_shader = bgfx::create_shader(&vs_data);
        let fs_shader = bgfx::create_shader(&fs_data);

        let shader_id = self.shader_id;
        
        // TODO: Check if shader is valid
        let program = bgfx::create_program(&vs_shader, &fs_shader, false);
        self.programs.insert(shader_id, program);
        self.shader_id += 1;

        Ok(ShaderProgram { handle: shader_id })
    }

    pub fn get_shader_program(&mut self, id: u64) -> Result<bgfx::Program> {
        if let Some(program) = self.programs.get(&id) {
            Ok(program.clone())
        } else {
            Err(InternalError::GenericError { text: "Shader program not found".into() })
        }
    }
}

