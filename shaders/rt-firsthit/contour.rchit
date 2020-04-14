#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

layout(set = 1, binding = 9, scalar) buffer readonly Colors { vec4 colors[]; };

struct ContourHit {
	uint colorIndex;
};

hitAttributeNV ContourHit hit;

layout(location = 0) rayPayloadInNV vec3 hitValue;

void main()
{
	vec3 color = colors[hit.colorIndex].rgb;
	color = pow(color, vec3(2.2));

	hitValue = color;
}
