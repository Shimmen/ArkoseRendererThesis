#version 460
#extension GL_NV_ray_tracing : require

struct SphereHit {
	vec3 color;
};

hitAttributeNV SphereHit hit;

layout(location = 0) rayPayloadInNV vec3 hitValue;

void main()
{
	// TODO: We could skip this shader and just fill rayPayloadInNV from intersection
	hitValue = hit.color;
}
