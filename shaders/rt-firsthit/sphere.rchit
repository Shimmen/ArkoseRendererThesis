#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "shared/RTData.h"
#include "shared/LightData.h"
#include "brdf.glsl"

struct SphereHit {
	vec3 normal;
	uint materialIndex;
};

hitAttributeNV SphereHit hit;

layout(location = 0) rayPayloadInNV vec3 hitValue;

void main()
{
	vec3 N = normalize(hit.normal);

	// TODO: Use materials!
	//uint materialIdx = hit.materialIndex;
	//vec3 color = vec3(1, 0, 1);

	hitValue = N * 0.5 + 0.5;
}
