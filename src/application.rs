use crate::manual::Mainloop;
use crate::ShaderProgram;
use crate::{io::IoFfiApi, io_handler::IoHandler};
use crate::{AppFfi, ApplicationSettings};
use bgfx::*;
use bgfx_rs::bgfx;
use core::ffi::c_void;
use crate::imgui;

extern "C" {
    fn c_create(settings: *const ApplicationSettings) -> *const c_void;
    fn c_destroy(data: *const c_void);
    fn c_pre_update(data: *const c_void);
    fn c_pre_update_create(data: *const c_void);
    fn c_post_update(data: *const c_void);
    fn c_should_close(data: *const c_void) -> bool;
    fn c_raw_window_handle(data: *const c_void) -> *mut c_void;
}

const WIDTH: u16 = 1280;
const HEIGHT: u16 = 720;

struct DearImguiRenderer {
    shader_program: ShaderProgram,
    layout: BuiltVertexLayout,
    sampler_uniform : bgfx::Uniform,
    font_atlas : bgfx::Texture,
    view_id: u16,
}

impl DearImguiRenderer {
    pub fn new(io_handler: &mut IoHandler) -> Self {
        let layout = bgfx::VertexLayoutBuilder::begin(bgfx::RendererType::Noop)
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float, AddArgs { normalized: true, as_int: false, })
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float, AddArgs { normalized: true, as_int: false, })
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, AddArgs { normalized: true, as_int: true, })
            .end();
            
        let shader_program = io_handler.load_shader_program_comp(
            "data/shaders/vs_ocornut_imgui.sc",
            "data/shaders/fs_ocornut_imgui.sc")
            .unwrap();

        let sampler_uniform = bgfx::Uniform::create("s_tex", bgfx::UniformType::Sampler, 1);
        let font_atlas = imgui::FontAtlas::build_r8_texture();

        let font_atlas = bgfx::create_texture_2d(
            font_atlas.width, 
            font_atlas.height, 
            false, 1, 
            bgfx::TextureFormat::R8, 
            0, 
            &Memory::copy(&font_atlas.data()));

        Self {
            shader_program,
            font_atlas,
            sampler_uniform,
            layout,
            view_id: 0xFF,
        }
    }

    fn render(&self, shader_prog: &bgfx::Program) {
        let draw_data = imgui::DrawData::get_data();
        let index_32 = false;

        let fb_width = draw_data.display_size[0] * draw_data.framebuffer_scale[0];
        let fb_height = draw_data.display_size[1] * draw_data.framebuffer_scale[1];

        if fb_width <= 0.0 || fb_height <= 0.0 {
            return;
        }

        {
            let x = draw_data.display_pos[0];
            let y = draw_data.display_pos[1];
            let width = draw_data.display_size[0];
            let height = draw_data.display_size[1];
            let projection = glam::Mat4::orthographic_lh(x, x + width, y + height, y, 0.0f32, 1000.0f32);
            bgfx::set_view_transform(self.view_id, &glam::Mat4::IDENTITY.as_ref(), &projection.as_ref());
            bgfx::set_view_rect(self.view_id, 0, 0, width as u16, height as u16);
        }

        bgfx::set_view_mode(self.view_id, bgfx::ViewMode::Sequential);

        let clip_pos = draw_data.display_pos;       // (0,0) unless using multi-viewports
        let clip_scale = draw_data.framebuffer_scale; // (1,1) unless using retina display which are often (2,2)

        for draw_list in draw_data.draw_lists() {
            let vertices_count = draw_list.vtx_buffer().len() as u32;
            let indices_count = draw_list.idx_buffer().len() as u32;

            if bgfx::get_avail_transient_vertex_buffer(vertices_count, &self.layout)
                != vertices_count
                || bgfx::get_avail_transient_index_buffer(indices_count, index_32) != indices_count
            {
                break;
            }

            let mut tvb = bgfx::TransientVertexBuffer::new();
            let mut tib = bgfx::TransientIndexBuffer::new();

            bgfx::alloc_transient_vertex_buffer(&mut tvb, vertices_count, &self.layout);
            bgfx::alloc_transient_index_buffer(&mut tib, indices_count, index_32);

            unsafe {
                std::ptr::copy_nonoverlapping(
                    draw_list.vtx_buffer().as_ptr() as *const u8,
                    tvb.data as *mut u8,
                    std::mem::size_of::<imgui::DrawVert>() * vertices_count as usize,
                );
            }
            unsafe {
                std::ptr::copy_nonoverlapping(
                    draw_list.idx_buffer().as_ptr() as *const u8,
                    tib.data as *mut u8,
                    std::mem::size_of::<imgui::ImDrawIdx>() * indices_count as usize,
                );
            }

            let encoder = bgfx::encoder_begin(false);
            for command in draw_list.commands() {
                match command {
                    imgui::DrawCmd::Elements { count, cmd_params } => {
                        let state = StateWriteFlags::RGB.bits()
                            | StateWriteFlags::A.bits()
                            | StateFlags::MSAA.bits()
                            | StateBlendFlags::SRC_ALPHA.bits()
                            | (StateBlendFlags::INV_SRC_ALPHA.bits() << 4)
                            | (StateBlendFlags::SRC_ALPHA.bits() << 8)
                            | (StateBlendFlags::INV_SRC_ALPHA.bits() << 12);
                        let clip_rect = [
                            (cmd_params.clip_rect[0] - clip_pos[0]) * clip_scale[0],
                            (cmd_params.clip_rect[1] - clip_pos[1]) * clip_scale[1],
                            (cmd_params.clip_rect[2] - clip_pos[0]) * clip_scale[0],
                            (cmd_params.clip_rect[3] - clip_pos[1]) * clip_scale[1],
                        ];
                        if clip_rect[0] < fb_width
                            && clip_rect[1] < fb_height
                            && clip_rect[2] >= 0.0f32
                            && clip_rect[3] >= 0.0f32
                        {
                            let xx = clip_rect[0].max(0.0f32) as u16;
                            let yy = clip_rect[1].max(0.0f32) as u16;
                            encoder.set_scissor(
                                xx,
                                yy,
                                (clip_rect[2].min(f32::MAX) as u16) - xx,
                                (clip_rect[3].min(f32::MAX) as u16) - yy,
                            );
                            encoder.set_state(state, 0);
                            encoder.set_texture(
                                0,
                                &self.sampler_uniform,
                                &self.font_atlas,
                                u32::MAX,
                            );
                            encoder.set_transient_vertex_buffer(
                                0,
                                &tvb,
                                cmd_params.vtx_offset as u32,
                                vertices_count,
                            );
                            encoder.set_transient_index_buffer(
                                &tib,
                                cmd_params.idx_offset as u32,
                                count as u32,
                            );
                            encoder.submit(
                                self.view_id,
                                shader_prog,
                                SubmitArgs::default(),
                            );
            
                            //dbg!("Render stuff");
                        }
                    }
                    imgui::DrawCmd::RawCallback { callback, raw_cmd } => unsafe {
                        //callback(draw_list.raw(), raw_cmd);
                    },
                    imgui::DrawCmd::ResetRenderState => {
                        //bgfx::reset(fb_width as u32, fb_height as u32, ResetArgs::default());
                    }
                }
            }
            bgfx::encoder_end(&encoder);
        }
    }
}


struct ApplicationState {
    c_data: *const c_void,
    c_user_data: *const c_void,
    io_handler: Box<IoHandler>,
    io_api: IoFfiApi,
    main_loop: Option<Mainloop>,
    settings: ApplicationSettings,
    imgui: Option<DearImguiRenderer>,
}

/*
        imgui_context.set_ini_filename(None);
        let mut io = imgui_context.io_mut();
        io.backend_flags = imgui::BackendFlags::RENDERER_HAS_VTX_OFFSET;
        let texture = {
            let mut fonts = imgui_context.fonts();
            let font_atlas = fonts.build_rgba32_texture();
            bgfx::create_texture_2d(font_atlas.width as u16, font_atlas.height as u16, false, 1, bgfx::TextureFormat::BGRA8, 0, &Memory::copy(&font_atlas.data))
        };
        Self {
            shader_program: {
                let vsh = bgfx::create_shader(&Memory::copy(get_shader_code!(VS_OCORNUT_IMGUI)));
                let fsh = bgfx::create_shader(&Memory::copy(get_shader_code!(FS_OCORNUT_IMGUI)));
                //let vsh = bgfx::create_shader(&Memory::copy(&vs_ocornut_imgui::vs_ocornut_imgui_glsl));
                //let fsh = bgfx::create_shader(&Memory::copy(&fs_ocornut_imgui::fs_ocornut_imgui_glsl));
                bgfx::create_program(&vsh, &fsh, false)
                //bgfx::create_program(&vsh, &fsh, true) //TODO: Why Segmentation fault if we destroy the shaders?
            },
            vertex_layout: {
                let layout = bgfx::VertexLayoutBuilder::new();
                layout.begin(bgfx::RendererType::Noop);
                layout.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float, AddArgs{ normalized: true, as_int: false });
                layout.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float, AddArgs{ normalized: true, as_int: false });
                layout.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, AddArgs{ normalized: true, as_int: true });
                layout.end();
                layout
            },
            sampler_uniform: {
                bgfx::Uniform::create("s_tex", bgfx::UniformType::Sampler, 1)
            },
            font_atlas: texture,
            view_id: 0xFF,
            frame_time: Instant::now()
        }
*/

impl ApplicationState {
    pub fn new(settings: &ApplicationSettings) -> Self {
        let mut io_handler = Box::new(IoHandler::new());
        let ffi_api = io_handler.get_ffi_api();

        let c_data = unsafe { c_create(settings) };

        if c_data.is_null() {
            panic!("Failed to create application");
        }

        let mut init = bgfx::Init::new();

        init.type_r = RendererType::OpenGL;
        init.resolution.width = WIDTH as u32;
        init.resolution.height = HEIGHT as u32;
        init.resolution.reset = ResetFlags::VSYNC.bits();
        init.platform_data = get_platform_data(c_data);

        if !bgfx::init(&init) {
            panic!("failed to init bgfx");
        }

        Self {
            c_data,
            c_user_data: std::ptr::null(),
            io_handler,
            main_loop: None,
            io_api: ffi_api,
            imgui: None,
            settings: settings.clone(),
        }
    }

    pub fn generate_frame(&mut self) {
        unsafe { c_pre_update(self.c_data) };

        bgfx::set_view_rect(0, 0, 0, WIDTH as _, HEIGHT as _);
        bgfx::touch(0);

        /*
        let verts_mem = unsafe { Memory::reference(&CUBE_VERTICES) };
        let index_mem = unsafe { Memory::reference(&CUBE_INDICES) };

        let vbh = bgfx::create_vertex_buffer(&verts_mem, &self.imgui.layout, BufferFlags::NONE.bits());
        let ibh = bgfx::create_index_buffer(&index_mem, BufferFlags::NONE.bits());
        */

        let state = (StateWriteFlags::R
            | StateWriteFlags::G
            | StateWriteFlags::B
            | StateWriteFlags::A
            | StateWriteFlags::Z)
            .bits()
            | StateDepthTestFlags::LESS.bits()
            | StateCullFlags::CW.bits();

        /*
        let shader_program = self.io_handler.shaders.get_shader_program(self.imgui.shader_program.handle).unwrap();

        //bgfx::set_transform(&transform.to_cols_array(), 1);
        bgfx::set_vertex_buffer(0, &vbh, 0, std::u32::MAX);
        bgfx::set_index_buffer(&ibh, 0, std::u32::MAX);

        bgfx::set_state(state, 0);
        bgfx::submit(0, &shader_program, SubmitArgs::default());
        */


        /*
        if let Some(main_callback) = self.main_loop {
            main_callback(self.c_user_data);
        }
        */

        unsafe { c_post_update(self.c_data) };

        if let Some(ref imgui) = self.imgui.as_ref() {
            // TODO: Fix me
            let shader_program = self.io_handler.shaders.get_shader_program(imgui.shader_program.handle).unwrap();
            imgui.render(shader_program);
        }

        bgfx::frame(false);
    }

    //pub fn mainloop(&mut self, callback: Mainloop, user_data: *mut c_void) {
    pub fn mainloop(&mut self, user_data: *mut c_void) {
        unsafe { c_pre_update_create(self.c_data) };

        let imgui = Some(DearImguiRenderer::new(& mut self.io_handler));
        self.imgui = imgui; 

        bgfx::set_view_clear(
            0,
            ClearFlags::COLOR.bits() | ClearFlags::DEPTH.bits(),
            SetViewClearArgs {
                rgba: 0x103030ff,
                ..Default::default()
            },
        );
        

        while !unsafe { c_should_close(self.c_data) } {
            self.generate_frame();
        }

        //bgfx::shutdown();
        unsafe { c_destroy(self.c_data) };
    }
}

fn get_platform_data(c_data: *const c_void) -> PlatformData {
    let mut pd = PlatformData::new();
    // TOOD. Support other OSes
    pd.nwh = unsafe { c_raw_window_handle(c_data) } as *mut _;
    return pd;
}

fn get_io_api(data: *const c_void, _api_ver: u32) -> *const IoFfiApi {
    println!("get_io_api()");
    let app = data.cast::<ApplicationState>();
    unsafe { &(*app).io_api }
}

//fn main_loop(callback: Mainloop, data: *const c_void, user_data: *mut c_void) -> bool {
fn create_main_loop(_callback: *const c_void, user_data: *mut c_void) -> bool {
    let wrapped_data = unsafe { &*(user_data as *const crate::manual::WrappedMainData) };
    let app = unsafe { &mut *(wrapped_data.priv_data as *mut ApplicationState) };
    //app.mainloop(generate_frame, user_data);
    app.mainloop(user_data);
    //let flowi = Flowi { api: app.c_data };
    //callback(&flowi, user_data)
    false
}

// This is to be compatible with emscripten_set_main_loop_arg later on
fn generate_frame(data: *mut ApplicationState) {
    let app = unsafe { &mut *(data as *mut ApplicationState) };
    app.generate_frame();

    //let app = unsafe { &*(data as *const ApplicationState) };
    //let app = data.cast::<ApplicationState>();
    //let flowi = Flowi { api: app.c_data };
    //callback(&flowi, user_data)
    //false
}

#[no_mangle]
fn fl_application_create_impl(settings: *const ApplicationSettings) -> *const AppFfi {
    let settings = unsafe { &*settings };
    let app = ApplicationState::new(settings);

    println!("fl_application_create_impl()");

    Box::into_raw(Box::new(AppFfi {
        priv_data: Box::into_raw(Box::new(app)) as *const _,
        io_get_api: get_io_api,
        main_loop: create_main_loop,
    }))
}
