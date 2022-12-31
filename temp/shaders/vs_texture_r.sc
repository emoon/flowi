$input a_position, a_texcoord0, a_color0
$output v_color0, v_texcoord0

uniform vec4 u_inv_res_tex;

#include "common.sh"

void main()
{
    vec2 pos = ((a_position.xy * u_viewTexel.xy) * vec2(2.0, -2.0)) - vec2(1.0, -1.0);
	gl_Position = vec4(pos.x, pos.y, 0.0, 1.0);

	v_color0 = a_color0;
	v_texcoord0 = a_texcoord0 * u_inv_res_tex.xy;
}


