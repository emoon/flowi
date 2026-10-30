// Viewport3d vertex stage: the one fixed mesh vertex format (FlMeshVertex) through a view-projection matrix.
// Unlit - no normals, no lighting, no per-object transform; the caller folds model into the matrix. GL only, outside the compute
// compat-header vocabulary.

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;

uniform mat4 view_projection;

out vec2 v_uv;
out vec4 v_color;

void main() {
    v_uv = in_uv;
    v_color = in_color;
    gl_Position = view_projection * vec4(in_pos, 1.0);
}
