$input a_position, a_texcoord0, a_color0
$output v_color0, v_texcoord0

#include "common.sh"

void main()
{
	v_texcoord0 = a_texcoord0;
	v_color0 = a_color0;
	//vec2 t = vec2(1.0 / 640.0, 1.0 / 400.0);

    vec2 pos = ((a_position.xy * u_viewTexel.xy) * vec2(2.0, -2.0)) - vec2(1.0, -1.0);
    //vec2 pos = ((a_position.xy * t.xy) * vec2(2.0, -2.0)) - vec2(1.0, -1.0);
	gl_Position = vec4(pos.x, pos.y, 0.0, 1.0);
}

