#version 460
#extension GL_NV_ray_tracing : require

struct ContourHit {
	vec3 normal;
};

hitAttributeNV ContourHit hit;

layout(location = 0) rayPayloadInNV vec3 hitValue;

void main()
{
	// TODO: Use correct colors!
	//vec3 color = pow(vec3(0.0, 0.15, 0.80), vec3(2.2)); // (hardcoded blue for the bunny test)
	vec3 color = hit.normal * 0.5 + 0.5;

	hitValue = color;
}
