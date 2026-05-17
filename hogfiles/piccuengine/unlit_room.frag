#version 450 core

uniform sampler2D colortexture;
uniform sampler2D lightmaptexture;
uniform int ao_class_value;

in vec2 outuv;
in vec2 outuv2;
in float outlight;
in float outalpha;

layout(location = 0) out vec4 color;
layout(location = 2) out vec4 post_mask;
layout(location = 3) out float ao_class;

void main()
{
	vec4 basecolor = texture(colortexture, outuv);
	color = vec4(basecolor.rgb * outlight, basecolor.a * outalpha);
	post_mask = vec4(0.0, 0.0, 0.0, 1.0);
	ao_class = float(clamp(ao_class_value, 0, 255)) / 255.0;
}
