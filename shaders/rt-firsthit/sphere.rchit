#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

#include <common.glsl>
#include <shared/SphericalHarmonics.h>

struct SphereHit {
	vec3 normal;
};

hitAttributeNV SphereHit hit;

layout(set = 1, binding = 5) buffer readonly SphereSH { SphericalHarmonics SHs[]; } setSHs[];

layout(location = 0) rayPayloadInNV vec3 toRaygen;

void main()
{
	SphericalHarmonics sh = setSHs[gl_InstanceCustomIndexNV].SHs[gl_PrimitiveID];
	toRaygen = sampleSphericalHarmonic(sh, hit.normal);
}
