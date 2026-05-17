#version 450 core

uniform sampler2D colortexture;
uniform sampler2D lightmaptexture;
uniform int ao_class_value;
uniform float ao_weight_value;
uniform int ao_capture_weight_mode;

struct specular
{
	vec4 bright_center;
	vec4 color;
};

layout(std140) uniform SpecularBlock
{
	int num_specular;
	int exponent;
	float strength;
	specular speculars[4];
} specular_data;

in vec2 outuv;
in vec2 outuv2;
in vec3 outpos;
in vec3 outnormal;
flat in vec3[4] outlightpos;

layout(location = 0) out vec4 color;
layout(location = 2) out vec4 post_mask;
layout(location = 3) out float ao_class;

void main()
{
	if (ao_capture_weight_mode != 0)
	{
		color = vec4(ao_weight_value, ao_weight_value, ao_weight_value, 1.0);
		post_mask = vec4(0.0);
		ao_class = ao_weight_value;
		return;
	}

	const float[4] weights = float[4](1.0, 0.66, 0.33, 0.25);
	vec4 basecolor = texture(colortexture, outuv);
	vec4 lmcolor = texture(lightmaptexture, outuv2);
	vec3 spec_color = vec3(0.0);

	vec3 pos = normalize(outpos);
	vec3 normal = normalize(outnormal);
	for (int i = 0; i < specular_data.num_specular; i++)
	{
		vec3 lightvec = normalize(outlightpos[i] + outpos);
		vec3 reflectlight = reflect(-lightvec, normal);

		spec_color += pow(max(dot(reflectlight, pos), 0.0), specular_data.exponent) *
			specular_data.speculars[i].color.xyz * lmcolor.rgb * specular_data.strength * weights[i];
	}
	color = vec4(spec_color, basecolor.a);
	post_mask = vec4(0.0);
	ao_class = float(clamp(ao_class_value, 0, 255)) / 255.0;
}
