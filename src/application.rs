use crate::{ApplicationSettings, AppFfi};
use core::ffi::c_void;
use crate::{io_handler::IoHandler, io::IoFfiApi};
use crate::manual::Mainloop;
use crate::ShaderProgram;
use bgfx_rs::bgfx;
use bgfx::*;

extern "C" {
    fn c_create(settings: *const ApplicationSettings) -> *const c_void;
    fn c_destroy(data: *const c_void);
    fn c_pre_update(data: *const c_void);
    fn c_post_update(data: *const c_void);
    fn c_should_close(data: *const c_void) -> bool;
    fn c_raw_window_handle(data: *const c_void) -> *mut c_void; 
}

struct DearImguiRenderer {
    shader_program: ShaderProgram,
}

const WIDTH: u16 = 1280;
const HEIGHT: u16 = 720;

struct ApplicationState {
    c_data: *const c_void,
    c_user_data: *const c_void,
    io_handler: Box<IoHandler>,
    io_api: IoFfiApi,
    main_loop: Option<Mainloop>,
    settings: ApplicationSettings,
    imgui_renderer: DearImguiRenderer,
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

        let imgui_renderer = DearImguiRenderer {
            shader_program: io_handler.load_shader_program_comp(
                "../../../data/shaders/vs_ocornut_imgui.sc", 
                "../../../data/shaders/fs_ocornut_imgui.sc", 
            ).unwrap(),
        };

        Self {
            c_data,
            c_user_data: std::ptr::null(),
            io_handler,
            main_loop: None,
            io_api: ffi_api,
            imgui_renderer,
            settings: settings.clone(),
        }
    }

    pub fn generate_frame(&mut self) {
        unsafe { c_pre_update(self.c_data) };

        bgfx::set_view_rect(0, 0, 0, WIDTH as _, HEIGHT as _);
        bgfx::touch(0);
        bgfx::frame(false);

        /*
        if let Some(main_callback) = self.main_loop {
            main_callback(self.c_user_data);
        }
        */

        c_post_update(self.c_data);
    }

    //pub fn mainloop(&mut self, callback: Mainloop, user_data: *mut c_void) {
    pub fn mainloop(&mut self, user_data: *mut c_void) {
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
