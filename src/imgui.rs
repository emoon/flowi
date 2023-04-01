// Most of this code is taken from imgui-rs
//
// Copyright (c) 2021 The imgui-rs Developers
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#![allow(nonstandard_style, clippy::all)]
use core::ffi as cty;
use std::slice;

extern "C" {
    fn imgui_get_draw_data() -> DrawData;
    //fn imgui_build_rgba32_texture() -> FontAtlas;
    fn imgui_build_r8_texture() -> FontAtlas;
}

pub type TextureID = u64;
    pub type ImDrawCallback = core::option::Option<unsafe extern "C" fn()>;

pub type ImGuiViewportFlags = cty::c_int;
pub type ImDrawListFlags = cty::c_int;

#[repr(C)]
#[derive(Debug, Copy, Clone, PartialEq)]
pub struct ImGuiViewport {
    pub Flags: ImGuiViewportFlags,
    pub Pos: ImVec2,
    pub Size: ImVec2,
    pub WorkPos: ImVec2,
    pub WorkSize: ImVec2,
    pub PlatformHandleRaw: *mut cty::c_void,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, PartialEq)]
pub struct ImDrawCmdHeader {
    pub ClipRect: ImVec4,
    pub TextureId: TextureID,
    pub VtxOffset: cty::c_uint,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Hash, PartialEq, Eq)]
pub struct ImVector_ImDrawChannel {
    pub Size: cty::c_int,
    pub Capacity: cty::c_int,
    pub Data: *mut cty::c_void,
}

//TODO: Configure
pub type ImDrawIdx = cty::c_ushort;
pub type ImU32 = cty::c_uint;

#[repr(C)]
#[derive(Debug, Copy, Clone, Hash, PartialEq, Eq)]
pub struct ImDrawListSplitter {
    pub _Current: cty::c_int,
    pub _Count: cty::c_int,
    pub _Channels: ImVector_ImDrawChannel,
}


#[repr(C)]
#[derive(Debug, Default, Copy, Clone, PartialEq)]
pub struct ImVec4 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub w: f32,
}

#[repr(C)]
#[derive(Debug, Default, Copy, Clone, PartialEq)]
pub struct ImVec2 {
    pub x: f32,
    pub y: f32,
}

#[repr(C)]
pub struct ImDrawCmd {
    pub ClipRect: ImVec4,
    pub TextureId: TextureID,
    pub VtxOffset: cty::c_uint,
    pub IdxOffset: cty::c_uint,
    pub ElemCount: cty::c_uint,
    pub UserCallback: ImDrawCallback,
    pub UserCallbackData: *mut cty::c_void,
}

/*
impl std::fmt::Debug for *mut ImDrawCmd {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("*mut ImDrawCmd")
            .field("Size", unsafe { &(*self).Size })
            .field("Capacity", unsafe { &(*self).Capacity })
            .field("Data", unsafe { &(*self).Data })
            .finish()
    }
}
*/

#[repr(C)]
#[derive(Debug, Default, Copy, Clone, PartialEq)]
pub struct DrawVert {
    pub pos: ImVec2,
    pub uv: ImVec2,
    pub col: ImU32,
}

#[repr(C)]
pub struct ImVector_ImDrawCmd {
    pub Size: cty::c_int,
    pub Capacity: cty::c_int,
    pub Data: *mut ImDrawCmd,
}


#[repr(C)]
pub struct ImVector_ImDrawVert {
    pub Size: cty::c_int,
    pub Capacity: cty::c_int,
    pub Data: *mut DrawVert,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Hash, PartialEq, Eq)]
pub struct ImVector_ImVec2 {
    pub Size: cty::c_int,
    pub Capacity: cty::c_int,
    pub Data: *mut ImVec2,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Hash, PartialEq, Eq)]
pub struct ImVector_ImTextureID {
    pub Size: cty::c_int,
    pub Capacity: cty::c_int,
    pub Data: *mut TextureID,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Hash, PartialEq, Eq)]
pub struct ImVector_ImDrawIdx {
    pub Size: cty::c_int,
    pub Capacity: cty::c_int,
    pub Data: *mut ImDrawIdx,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Hash, PartialEq, Eq)]
pub struct ImVector_ImVec4 {
    pub Size: cty::c_int,
    pub Capacity: cty::c_int,
    pub Data: *mut ImVec4,
}

#[repr(C)]
pub struct DrawList {
    pub CmdBuffer: ImVector_ImDrawCmd,
    pub IdxBuffer: ImVector_ImDrawIdx,
    pub VtxBuffer: ImVector_ImDrawVert,
    pub Flags: ImDrawListFlags,

    /// [Internal, used while building lists]
    _VtxCurrentIdx: cty::c_uint,
    _Data: *mut cty::c_void,
    _OwnerName: *const cty::c_char,
    _VtxWritePtr: *mut cty::c_void,
    _IdxWritePtr: *mut cty::c_void,
    _ClipRectStack: ImVector_ImVec4,
    _TextureIdStack: ImVector_ImTextureID,
    _Path: ImVector_ImVec2,
    _CmdHeader: ImDrawCmdHeader,
    _Splitter: ImDrawListSplitter,
    _FringeScale: f32,
}

#[repr(C)]
pub(crate) struct FontAtlas {
    pub(crate) width: u16,
    pub(crate) height: u16,
    data_size: u32,
    data: *const cty::c_void, 
}

impl FontAtlas {
    /*
    pub(crate) fn build_rgba32_texture() -> Self {
        unsafe { imgui_build_rgba32_texture() }
    }
    */

    pub(crate) fn build_r8_texture() -> Self {
        unsafe { imgui_build_r8_texture() }
    }

    pub(crate) fn data(&self) -> &[u8] {
        unsafe {
            slice::from_raw_parts(
                self.data as *const u8,
                self.data_size as usize,
            )
        }
    }
}


/// All draw data to render a Dear ImGui frame. Taken from imgui-rs
#[repr(C)]
#[derive(Debug)]
pub struct DrawData {
    /// Only valid after render() is called and before the next new frame() is called.
    valid: bool,
    /// Number of DrawList to render.
    cmd_lists_count: i32,
    /// For convenience, sum of all draw list index buffer sizes.
    pub total_idx_count: i32,
    /// For convenience, sum of all draw list vertex buffer sizes.
    pub total_vtx_count: i32,
    // Array of DrawList.
    cmd_lists: *mut *mut DrawList,
    /// Upper-left position of the viewport to render.
    ///
    /// (= upper-left corner of the orthogonal projection matrix to use)
    pub display_pos: [f32; 2],
    /// Size of the viewport to render.
    ///
    /// (= display_pos + display_size == lower-right corner of the orthogonal matrix to use)
    pub display_size: [f32; 2],
    /// Amount of pixels for each unit of display_size.
    ///
    /// Based on io.display_frame_buffer_scale. Typically [1.0, 1.0] on normal displays, and
    /// [2.0, 2.0] on Retina displays, but fractional values are also possible.
    pub framebuffer_scale: [f32; 2],

    owner_viewport: *mut ImGuiViewport,
}

impl DrawData {
    #[inline]
    pub fn get_data() -> DrawData {
        unsafe { imgui_get_draw_data() } 
    }

    /// Returns an iterator over the draw lists included in the draw data.
    #[inline]
    pub fn draw_lists(&self) -> DrawListIterator<'_> {
        unsafe {
            DrawListIterator {
                iter: self.cmd_lists().iter(),
            }
        }
    }

    #[inline]
    pub(crate) unsafe fn cmd_lists(&self) -> &[*const DrawList] {
        slice::from_raw_parts(
            self.cmd_lists as *const *const DrawList,
            self.cmd_lists_count as usize,
        )
    }
}

/// Iterator over draw lists
pub struct DrawListIterator<'a> {
    iter: std::slice::Iter<'a, *const DrawList>,
}

impl<'a> Iterator for DrawListIterator<'a> {
    type Item = &'a DrawList;

    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next().map(|&ptr| unsafe { &*ptr })
    }
}

impl DrawList {
    #[inline]
    pub(crate) unsafe fn cmd_buffer(&self) -> &[ImDrawCmd] {
        slice::from_raw_parts(
            self.CmdBuffer.Data as *const ImDrawCmd,
            self.CmdBuffer.Size as usize,
        )
    }
    #[inline]
    pub fn idx_buffer(&self) -> &[ImDrawIdx] {
        unsafe {
            slice::from_raw_parts(
                self.IdxBuffer.Data as *const ImDrawIdx,
                self.IdxBuffer.Size as usize,
            )
        }
    }
    #[inline]
    pub fn vtx_buffer(&self) -> &[DrawVert] {
        unsafe {
            slice::from_raw_parts(
                self.VtxBuffer.Data as *const DrawVert,
                self.VtxBuffer.Size as usize,
            )
        }
    }

    #[inline]
    pub fn commands(&self) -> DrawCmdIterator<'_> {
        unsafe {
            DrawCmdIterator {
                iter: self.cmd_buffer().iter(),
            }
        }
    }
}

pub struct DrawCmdIterator<'a> {
    iter: std::slice::Iter<'a, ImDrawCmd>,
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub struct DrawCmdParams {
    /// left, up, right, down
    pub clip_rect: [f32; 4],
    pub texture_id: TextureID,
    pub vtx_offset: usize,
    pub idx_offset: usize,
}

/// A draw command
#[derive(Debug)]
pub enum DrawCmd {
    Elements {
        /// The number of indices used for this draw command
        count: usize,
        cmd_params: DrawCmdParams,
    },
    ResetRenderState,
    RawCallback {
        callback: unsafe extern "C" fn(),
        raw_cmd: *const cty::c_void,
    },
}

impl<'a> Iterator for DrawCmdIterator<'a> {
    type Item = DrawCmd;

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next().map(|cmd| {
            let cmd_params = DrawCmdParams {
                clip_rect: [cmd.ClipRect.x, cmd.ClipRect.y, cmd.ClipRect.z, cmd.ClipRect.w],
                texture_id: cmd.TextureId,
                vtx_offset: cmd.VtxOffset as usize,
                idx_offset: cmd.IdxOffset as usize,
            };
            match cmd.UserCallback {
                Some(raw_callback) if raw_callback as usize == -1isize as usize => {
                    DrawCmd::ResetRenderState
                }
                Some(raw_callback) => DrawCmd::RawCallback {
                    callback: raw_callback,
                    raw_cmd: std::ptr::null(),
                },
                None => DrawCmd::Elements {
                    count: cmd.ElemCount as usize,
                    cmd_params,
                },
            }
        })
    }
}

