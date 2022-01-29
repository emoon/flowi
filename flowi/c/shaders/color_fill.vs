$input a_position, a_color0
$output v_color0

#include "common.sh"

void main() {
	v_color0 = a_color0;
    vec2 pos = ((a_position * u_viewTexel.xy) * vec2(2.0, -2.0)) - vec2(1.0, -1.0);
	gl_Position = vec4(pos.x, pos.y, 0.0, 1.0);
}
