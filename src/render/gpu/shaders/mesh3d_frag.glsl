// Viewport3d fragment stage: unlit vertex color, optionally modulated by a texture the mesh's UVs address.
//
// The output is premultiplied: the viewport's color target is drawn back as an ordinary image, and the raster
// pass assumes premultiplied texels. Fragments the geometry never covers keep the transparent clear.

in vec2 v_uv;
in vec4 v_color;

uniform sampler2D mesh_texture;
// 0 = vertex color only (nothing is bound to mesh_texture), 1 = modulate by the texture.
uniform int has_texture;

out vec4 out_color;

void main() {
    vec4 color = v_color;
    if (has_texture != 0) {
        color *= texture(mesh_texture, v_uv);
    }
    out_color = vec4(color.rgb * color.a, color.a);
}
