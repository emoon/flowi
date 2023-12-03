use bgfx::*;
use bgfx_rs::bgfx;
use flowi_core::ApplicationSettings;
use raw_window_handle::RawWindowHandle;
use crate::application::Renderer;
use flowi_core::imgui::{DrawData, DrawCmd, FontAtlas, DrawVert, ImDrawIdx};
use glam;

static VS_IMGUI_GLSL: &[u8] = include_bytes!("../data/shaders/vs_ocornut_imgui_glsl.bin");
static FS_IMGUI_GLSL: &[u8] = include_bytes!("../data/shaders/fs_ocornut_imgui_glsl.bin");

static VS_IMGUI_ESSL: &[u8] = include_bytes!("../data/shaders/vs_ocornut_imgui_essl.bin");
static FS_IMGUI_ESSL: &[u8] = include_bytes!("../data/shaders/fs_ocornut_imgui_essl.bin");

static VS_IMGUI_DX11: &[u8] = include_bytes!("../data/shaders/vs_ocornut_imgui_dx11.bin");
static FS_IMGUI_DX11: &[u8] = include_bytes!("../data/shaders/fs_ocornut_imgui_dx11.bin");

static VS_IMGUI_MTL: &[u8] = include_bytes!("../data/shaders/vs_ocornut_imgui_mtl.bin");
static FS_IMGUI_MTL: &[u8] = include_bytes!("../data/shaders/fs_ocornut_imgui_mtl.bin");

static VS_IMGUI_SPV: &[u8] = include_bytes!("../data/shaders/vs_ocornut_imgui_spv.bin");
static FS_IMGUI_SPV: &[u8] = include_bytes!("../data/shaders/fs_ocornut_imgui_spv.bin");

pub(crate) struct BgfxRenderer {
    shader_program: bgfx::Program,
    layout: BuiltVertexLayout,
    sampler_uniform : bgfx::Uniform,
    font_atlas : bgfx::Texture,
    old_size: (u32, u32),
    view_id: u16,
}

#[cfg(target_os = "linux")]
fn get_render_type() -> RendererType {
    RendererType::OpenGL
}

#[cfg(not(target_os = "linux"))]
fn get_render_type() -> RendererType {
    RendererType::Count
}

pub fn get_platform_data(handle: &RawWindowHandle) -> PlatformData {
    let mut pd = PlatformData::new();

    match handle {
        #[cfg(any(
            target_os = "linux",
            target_os = "dragonfly",
            target_os = "freebsd",
            target_os = "netbsd",
            target_os = "openbsd"
        ))]
        RawWindowHandle::Xlib(data) => {
            pd.nwh = data.window as *mut _;
        }
        #[cfg(any(
            target_os = "linux",
            target_os = "dragonfly",
            target_os = "freebsd",
            target_os = "netbsd",
            target_os = "openbsd"
        ))]
        RawWindowHandle::Wayland(data) => {
            pd.ndt = data.surface; // same as window, on wayland there ins't a concept of windows
        }

        #[cfg(target_os = "macos")]
        RawWindowHandle::AppKit(data) => {
            pd.nwh = data.ns_window;
        }
        #[cfg(target_os = "windows")]
        RawWindowHandle::Win32(data) => {
            pd.nwh = data.hwnd;
        }
        _ => panic!("Unsupported Window Manager"),
    }

    return pd;
}

impl BgfxRenderer {
    fn get_imgui_shader() -> (&'static [u8], &'static [u8]) { 
        match bgfx::get_renderer_type() {
            RendererType::Direct3D11 => (VS_IMGUI_DX11, FS_IMGUI_DX11), 
            RendererType::OpenGL => (VS_IMGUI_GLSL, FS_IMGUI_GLSL), 
            RendererType::OpenGLES => (VS_IMGUI_ESSL, FS_IMGUI_ESSL), 
            RendererType::Metal => (VS_IMGUI_MTL, FS_IMGUI_MTL), 
            RendererType::Vulkan => (VS_IMGUI_SPV, FS_IMGUI_SPV), 
            e => panic!("Unsupported render type {:#?}", e),
        }
    }

    fn compile_program(vs_ps: (&[u8], &[u8])) -> bgfx::Program {
        let vs_data = Memory::copy(vs_ps.0);
        let ps_data = Memory::copy(vs_ps.1);

        let vs_shader = bgfx::create_shader(&vs_data);
        let ps_shader = bgfx::create_shader(&ps_data);

        bgfx::create_program(&vs_shader, &ps_shader, false)
    }
}

impl Renderer for BgfxRenderer {
    fn new(settings: &ApplicationSettings, window: &RawWindowHandle) -> Self {
        let mut init = Init::new();

        init.type_r = get_render_type();
        init.resolution.width = settings.width;
        init.resolution.height = settings.height;
        init.resolution.reset = ResetFlags::VSYNC.bits();
        init.platform_data = get_platform_data(window);

        if !bgfx::init(&init) {
            panic!("failed to init bgfx");
        }

        bgfx::set_debug(DebugFlags::TEXT.bits());
        bgfx::set_view_clear(
            0,
            ClearFlags::COLOR.bits() | ClearFlags::DEPTH.bits(),
            SetViewClearArgs {
                rgba: 0x103030ff,
                ..Default::default()
            },
        );

        let layout = bgfx::VertexLayoutBuilder::begin(bgfx::RendererType::Noop)
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float, AddArgs { normalized: true, as_int: false, })
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float, AddArgs { normalized: true, as_int: false, })
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, AddArgs { normalized: true, as_int: true, })
            .end();
            
        let shader_program = Self::compile_program(Self::get_imgui_shader());

        let sampler_uniform = bgfx::Uniform::create("s_tex", bgfx::UniformType::Sampler, 1);
        let font_atlas = FontAtlas::build_rgba32_texture();

        let font_atlas = bgfx::create_texture_2d(
            font_atlas.width, 
            font_atlas.height, 
            false, 1, 
            bgfx::TextureFormat::RGBA8,
            0, 
            &Memory::copy(&font_atlas.data()));

        Self {
            shader_program,
            font_atlas,
            sampler_uniform,
            layout,
            view_id: 0xFF,
            old_size: (0, 0),
        }
    }

    fn render(&mut self) {
        let draw_data = DrawData::get_data();
        let index_32 = false;

        let fb_width = draw_data.display_size[0] * draw_data.framebuffer_scale[0];
        let fb_height = draw_data.display_size[1] * draw_data.framebuffer_scale[1];

        if fb_width <= 0.0 || fb_height <= 0.0 {
            return;
        }

        let size = (draw_data.display_size[0] as _, draw_data.display_size[1] as _);

        if self.old_size != size {
            bgfx::reset(size.0 as _, size.1 as _, ResetArgs::default());
            self.old_size = size;
        }

        bgfx::set_view_mode(self.view_id, bgfx::ViewMode::Sequential);
        bgfx::set_view_clear(
            self.view_id,
            ClearFlags::COLOR.bits() | ClearFlags::DEPTH.bits(),
            SetViewClearArgs {
                rgba: 0x103030ff,
                ..Default::default()
            },
        );

        {
            let x = draw_data.display_pos[0];
            let y = draw_data.display_pos[1];
            let width = draw_data.display_size[0];
            let height = draw_data.display_size[1];
            let projection = glam::Mat4::orthographic_lh(x, x + width, y + height, y, 0.0f32, 1000.0f32);
            bgfx::set_view_transform(self.view_id, &glam::Mat4::IDENTITY.as_ref(), &projection.as_ref());
            bgfx::set_view_rect(self.view_id, 0, 0, width as u16, height as u16);
        }

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
                    std::mem::size_of::<DrawVert>() * vertices_count as usize,
                );
            }
            unsafe {
                std::ptr::copy_nonoverlapping(
                    draw_list.idx_buffer().as_ptr() as *const u8,
                    tib.data as *mut u8,
                    std::mem::size_of::<ImDrawIdx>() * indices_count as usize,
                );
            }

            //dbg!(clip_scale);

            let encoder = bgfx::encoder_begin(false);
            for command in draw_list.commands() {
                match command {
                    DrawCmd::Elements { count, cmd_params } => {
                        let state = 
                            StateWriteFlags::RGB.bits()
                            | StateWriteFlags::A.bits()
                            | StateFlags::MSAA.bits()
                            | bgfx::state_blend_func(
                                StateBlendFlags::SRC_ALPHA, 
                                StateBlendFlags::INV_SRC_ALPHA);

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
                            let ww = (clip_rect[2].min(65535.0) - xx as f32) as u16;
                            let hh = (clip_rect[3].min(65535.0) - yy as f32) as u16;

                            //dbg!(xx, yy, ww, hh);

                            encoder.set_scissor(xx, yy, ww, hh);
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
                                &self.shader_program,
                                SubmitArgs::default(),
                            );
                        }
                    }
                    DrawCmd::RawCallback { callback: _, raw_cmd: _ } => {
                        //callback(draw_list.raw(), raw_cmd);
                    },
                    DrawCmd::ResetRenderState => {
                        bgfx::reset(fb_width as u32, fb_height as u32, ResetArgs::default());
                    }
                }
            }
            bgfx::encoder_end(&encoder);
        }

        /*
        //bgfx::set_view_rect(0, 0, 0, size.0 as _, size.1 as _);
        bgfx::touch(0);

        bgfx::dbg_text_clear(DbgTextClearArgs::default());

        bgfx::dbg_text(0, 1, 0x0f, "Color can be changed with ANSI \x1b[9;me\x1b[10;ms\x1b[11;mc\x1b[12;ma\x1b[13;mp\x1b[14;me\x1b[0m code too.");
        bgfx::dbg_text(80, 1, 0x0f, "\x1b[;0m    \x1b[;1m    \x1b[; 2m    \x1b[; 3m    \x1b[; 4m    \x1b[; 5m    \x1b[; 6m    \x1b[; 7m    \x1b[0m");
        bgfx::dbg_text(80, 2, 0x0f, "\x1b[;8m    \x1b[;9m    \x1b[;10m    \x1b[;11m    \x1b[;12m    \x1b[;13m    \x1b[;14m    \x1b[;15m    \x1b[0m");
        bgfx::dbg_text(
            0,
            4,
            0x3f,
            "Description: Initialization and debug text with bgfx-rs Rust API.",
        );
        */
        
        bgfx::frame(false);
    }
}

