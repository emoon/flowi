///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: the hardware raster path behind Viewport3d and Mesh - the backend's only non-compute pipeline.
//
// Per viewport: a color + depth framebuffer, sized to the element's layout rect and rebuilt on resize. Per
// mesh: a VAO over an interleaved vertex buffer plus a 16-bit index buffer, created once and drawn from any
// viewport, as triangles or lines depending on how it was created. One vertex/fragment program, unlit,
// hardware depth test (per draw), painter's order, no blending.
//
// The color target is registered in the image-slot table under the handle the core minted for it, so the UI's
// raster pass samples it like a large static image and the slot table owns the texture's lifetime - this file
// never deletes it. Everything here runs on the render thread with the backend's GL context current, during the
// frame callback and so ahead of the UI raster pass that samples the result.

#include <core/log.h>
#include "gpu_renderer_internal.h"
#include "gpu_shaders.h"
#include <ui/image_registry.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Vertex attribute layout of FlMeshVertex, the one fixed format meshes use.
#define MESH_ATTRIB_POS 0
#define MESH_ATTRIB_UV 1
#define MESH_ATTRIB_COLOR 2
#define MESH_UV_OFFSET 12    // 3 floats of position
#define MESH_COLOR_OFFSET 20 // + 2 floats of uv

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Delete a viewport's framebuffer, depth renderbuffer and color slot. The color texture goes back through the
// slot table, which queues it for deletion with the context current.
static void release_targets(GpuShared* self, GpuViewport3d* viewport) {
    if (viewport->fbo == 0) {
        return;
    }
    const GpuGl* gl = &self->gl;
    gl->DeleteFramebuffers(1, &viewport->fbo);
    gl->DeleteRenderbuffers(1, &viewport->depth_rb);
    gpu_image_slots_release_static(&self->cpu.image_slots, viewport->color);
    image_registry_remove((ImageHandle)viewport->color);
    viewport->fbo = 0;
}

// A full table gives up the viewport submitted least recently, provided it sat out at least one whole frame;
// a viewport submitted this frame or the last is live and never displaced.
static GpuViewport3d* find_viewport(GpuShared* self, u32 id) {
    const u64 frame = self->viewport3d_frame;
    GpuViewport3d* stale = nullptr;
    for (u32 i = 0; i < self->viewport3d_count; i++) {
        GpuViewport3d* viewport = &self->viewport3d[i];
        if (viewport->id == id) {
            viewport->last_frame = frame;
            return viewport;
        }
        if (viewport->last_frame + 1 < frame && (stale == nullptr || viewport->last_frame < stale->last_frame)) {
            stale = viewport;
        }
    }

    GpuViewport3d* viewport = nullptr;
    if (self->viewport3d_count < GPU_VIEWPORT3D_CAP) {
        viewport = &self->viewport3d[self->viewport3d_count++];
    } else if (stale != nullptr) {
        release_targets(self, stale);
        viewport = stale;
    } else {
        log_error("gpu backend: more than %d live 3D viewports - viewport %u is not rendered", GPU_VIEWPORT3D_CAP,
                  (unsigned)id);
        return nullptr;
    }
    *viewport = (GpuViewport3d) { .id = id, .last_frame = frame };
    return viewport;
}

// (Re)build a viewport's color + depth attachments at width x height and publish the color texture under
// color. The previous color texture goes back through the slot table, which queues it for deletion with the
// context current; the depth renderbuffer and the FBO are this file's to delete.
//
// Either the whole target is built or none of it is: the fields the caller's resize check reads are committed
// last, after the only step that can fail, and that failure path clears fbo back to zero.
static void setup_targets(GpuShared* self, GpuViewport3d* viewport, FlImageHandle color, u32 width, u32 height) {
    const GpuGl* gl = &self->gl;

    release_targets(self, viewport);
    *viewport
        = (GpuViewport3d) { .id = viewport->id,
                            .last_frame = viewport->last_frame }; // no target until every step below has succeeded

    // Readiness is dropped for the whole rebuild; the mark_realized at the end restores it.
    image_registry_remove((ImageHandle)color);

    GLuint color_tex = create_slot_texture(gl, width, height, false, false);

    // Publish the target as an ordinary image: the slot table resolves the handle for the raster pass and owns the
    // texture from here on. A full table fails here, before anything else is built.
    if (gpu_image_slots_register_static(&self->cpu.image_slots, color, width, height, (u32)color_tex) < 0) {
        glDeleteTextures(1, &color_tex); // table full - register logged the reason
        return;                          // fbo stays 0, so the next submit retries instead of drawing into nothing
    }

    GLuint depth_rb = 0;
    gl->GenRenderbuffers(1, &depth_rb);
    gl->BindRenderbuffer(GL_RENDERBUFFER, depth_rb);
    gl->RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (GLsizei)width, (GLsizei)height);
    gl->BindRenderbuffer(GL_RENDERBUFFER, 0);

    GLuint fbo = 0;
    gl->GenFramebuffers(1, &fbo);
    gl->BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex, 0);
    gl->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);
    bool complete = gl->CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!complete) {
        log_error("gpu backend: 3D viewport framebuffer incomplete at %ux%u", (unsigned)width, (unsigned)height);
        gl->DeleteFramebuffers(1, &fbo);
        gl->DeleteRenderbuffers(1, &depth_rb);
        gpu_image_slots_release_static(&self->cpu.image_slots, color);
        return; // same contract as above: nothing committed, so the next submit retries
    }

    viewport->fbo = fbo;
    viewport->depth_rb = depth_rb;
    viewport->color = color;
    viewport->width = width;
    viewport->height = height;

    // The readiness ack is what opens the drawing gate for the element, so it comes only once the target is
    // whole and committed.
    image_registry_mark_realized((ImageHandle)color, (int)width, (int)height);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Retained meshes. The handle is the table index plus one, so zero stays the invalid handle.

FlMeshHandle gpu_mesh_create(void* user_data, const FlMeshVertex* vertices, u32 vertex_count, const u16* indices,
                             u32 index_count, FlMeshTopology topology) {
    GpuShared* self = user_data;
    const GpuGl* gl = &self->gl;

    int slot = -1;
    for (u32 i = 0; i < GPU_MESH_CAP; i++) {
        if (self->meshes[i].vao == 0) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        log_error("gpu backend: mesh table full (%d) - mesh not created", GPU_MESH_CAP);
        return FL_MESH_HANDLE_INVALID;
    }

    GpuMesh* mesh = &self->meshes[slot];
    gl->GenVertexArrays(1, &mesh->vao);
    gl->BindVertexArray(mesh->vao);

    GLuint buffers[2];
    gl->GenBuffers(2, buffers);
    mesh->vbo = buffers[0];
    mesh->ibo = buffers[1];

    gl->BindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * sizeof(FlMeshVertex)), vertices,
                   GL_STATIC_DRAW);
    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ibo);
    gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)index_count * sizeof(u16)), indices, GL_STATIC_DRAW);

    gl->EnableVertexAttribArray(MESH_ATTRIB_POS);
    gl->VertexAttribPointer(MESH_ATTRIB_POS, 3, GL_FLOAT, GL_FALSE, sizeof(FlMeshVertex), (const void*)0);
    gl->EnableVertexAttribArray(MESH_ATTRIB_UV);
    gl->VertexAttribPointer(MESH_ATTRIB_UV, 2, GL_FLOAT, GL_FALSE, sizeof(FlMeshVertex), (const void*)MESH_UV_OFFSET);
    gl->EnableVertexAttribArray(MESH_ATTRIB_COLOR);
    gl->VertexAttribPointer(MESH_ATTRIB_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(FlMeshVertex),
                            (const void*)MESH_COLOR_OFFSET);

    gl->BindVertexArray(0);
    mesh->index_count = index_count;
    mesh->primitive = topology == FlMeshTopology_Lines ? GL_LINES : GL_TRIANGLES;
    return (FlMeshHandle)(slot + 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static GpuMesh* resolve_mesh(GpuShared* self, FlMeshHandle handle) {
    if (handle == FL_MESH_HANDLE_INVALID || handle > GPU_MESH_CAP) {
        return nullptr;
    }
    GpuMesh* mesh = &self->meshes[handle - 1];
    return mesh->vao != 0 ? mesh : nullptr;
}

void gpu_mesh_destroy(void* user_data, FlMeshHandle handle) {
    GpuShared* self = user_data;
    GpuMesh* mesh = resolve_mesh(self, handle);
    if (mesh == nullptr) {
        return;
    }
    const GpuGl* gl = &self->gl;
    GLuint buffers[2] = { mesh->vbo, mesh->ibo };
    gl->DeleteBuffers(2, buffers);
    gl->DeleteVertexArrays(1, &mesh->vao);
    *mesh = (GpuMesh) { 0 }; // vao 0 frees the slot for reuse
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render one viewport's draw list. Opaque and unsorted: the depth buffer resolves visibility for
// depth-tested draws, so their submission order does not matter, and blending stays off. Draws that opt out of
// the depth test are the exception - they land in submission order, over whatever is already there.

void gpu_viewport3d_submit(void* user_data, u32 viewport_id, FlImageHandle color, u32 width, u32 height,
                           const f32* view_projection, const FlViewport3dDraw* draws, u32 draw_count) {
    GpuShared* self = user_data;
    const GpuGl* gl = &self->gl;

    GpuViewport3d* viewport = find_viewport(self, viewport_id);
    if (viewport == nullptr) {
        return;
    }
    if (viewport->fbo == 0 || viewport->width != width || viewport->height != height || viewport->color != color) {
        setup_targets(self, viewport, color, width, height);
    }
    if (viewport->fbo == 0) {
        // The target could not be built (setup_targets logged why). Bailing is not optional: framebuffer 0 is
        // the default framebuffer, so falling through would draw this viewport's scene over the window itself.
        return;
    }

    gl->BindFramebuffer(GL_FRAMEBUFFER, viewport->fbo);
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    // Transparent clear: fragments the geometry never covers let the UI behind the viewport show through.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE); // the caller owns winding; a lightmapper wants to see both sides of a face

    gl->UseProgram(self->mesh3d_prog);
    gl->UniformMatrix4fv(self->u_mesh3d_view_projection, 1, GL_FALSE, view_projection);
    glActiveTexture(GL_TEXTURE0);

    bool depth_test = false; // matches the state the pass is entered and left in, so the first draw sets it
    glDisable(GL_DEPTH_TEST);

    for (u32 i = 0; i < draw_count; i++) {
        GpuMesh* mesh = resolve_mesh(self, draws[i].mesh);
        if (mesh == nullptr) {
            continue; // destroyed between submission and render, or never created
        }

        // A draw with the depth test off both skips the test and writes no depth, which is what an overlay
        // wants: it stays visible through the geometry it annotates without occluding anything drawn after it.
        if (draws[i].depth_test != depth_test) {
            depth_test = draws[i].depth_test;
            if (depth_test) {
                glEnable(GL_DEPTH_TEST);
            } else {
                glDisable(GL_DEPTH_TEST);
            }
        }

        // A mesh texture is always a dedicated (never atlased) image, so it resolves to a whole slot texture
        // and the mesh's UVs address it directly - no atlas sub-rect transform is involved.
        int slot_index = gpu_image_slots_find_static(&self->cpu.image_slots, draws[i].texture);
        gl->Uniform1i(self->u_mesh3d_has_texture, slot_index >= 0 ? 1 : 0);
        if (slot_index >= 0) {
            glBindTexture(GL_TEXTURE_2D, (GLuint)self->cpu.image_slots.images[slot_index].tex);
            // Nearest is what a lightmapper asks for: it wants to see the texels it baked, not a blend of them.
            gl->BindSampler(0, draws[i].filter == FlImageFilter_Nearest ? self->sampler_nearest : self->sampler_linear);
        }

        gl->BindVertexArray(mesh->vao);
        glDrawElements(mesh->primitive, (GLsizei)mesh->index_count, GL_UNSIGNED_SHORT, (const void*)0);
    }

    // Hand the context back exactly as the compute passes expect it: default framebuffer, no depth test, no
    // bound VAO or sampler on the scratch unit.
    gl->BindVertexArray(0);
    gl->BindSampler(0, 0);
    glDisable(GL_DEPTH_TEST);
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);

    // The color target is consumed by texture fetches in the UI raster pass later this frame.
    gl->MemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compile the unlit program and resolve its uniforms. Returns false (logged) when either stage fails to
// compile or the program fails to link, which leaves the backend without a 3D seam.

bool gpu_viewport3d_create(GpuShared* self) {
    const GpuGl* gl = &self->gl;
    const char* srcs[GPU_SHADER_MAX_SOURCES];

    GLuint vert = gpu_compile_shader(gl, GL_VERTEX_SHADER, srcs, gpu_mesh3d_vertex_sources(srcs), "mesh vertex shader");
    GLuint frag
        = gpu_compile_shader(gl, GL_FRAGMENT_SHADER, srcs, gpu_mesh3d_fragment_sources(srcs), "mesh fragment shader");
    if (vert == 0 || frag == 0) {
        gl->DeleteShader(vert); // DeleteShader ignores 0, so this releases exactly the stage that compiled
        gl->DeleteShader(frag);
        return false;
    }

    GLuint prog = gl->CreateProgram();
    gl->AttachShader(prog, vert);
    gl->AttachShader(prog, frag);
    gl->LinkProgram(prog);
    gl->DeleteShader(vert); // the program holds its own references
    gl->DeleteShader(frag);

    GLint ok = 0;
    gl->GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[2048];
        gl->GetProgramInfoLog(prog, sizeof(info), NULL, info);
        log_error("gpu backend: mesh shader link failed: %s", info);
        gl->DeleteProgram(prog);
        return false;
    }

    self->mesh3d_prog = prog;
    self->u_mesh3d_view_projection = gl->GetUniformLocation(prog, "view_projection");
    self->u_mesh3d_has_texture = gl->GetUniformLocation(prog, "has_texture");
    gl->UseProgram(prog);
    gl->Uniform1i(gl->GetUniformLocation(prog, "mesh_texture"), 0); // the scratch unit, free while this pass runs
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tear down the framebuffers, depth renderbuffers, mesh buffers, and the program. The viewports' color
// textures are NOT freed here - they live in the slot table, which the backend's destroy drains.

void gpu_viewport3d_destroy(GpuShared* self) {
    const GpuGl* gl = &self->gl;
    for (u32 i = 0; i < self->viewport3d_count; i++) {
        GpuViewport3d* viewport = &self->viewport3d[i];
        if (viewport->fbo != 0) {
            gl->DeleteFramebuffers(1, &viewport->fbo);
            gl->DeleteRenderbuffers(1, &viewport->depth_rb);
        }
    }
    self->viewport3d_count = 0;

    for (u32 i = 0; i < GPU_MESH_CAP; i++) {
        gpu_mesh_destroy(self, (FlMeshHandle)(i + 1));
    }
    gl->DeleteProgram(self->mesh3d_prog);
}
