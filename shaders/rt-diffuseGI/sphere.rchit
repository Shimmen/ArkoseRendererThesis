#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include <brdf.glsl>
#include <common.glsl>
#include <lighting.glsl>
#include <shared/RTData.h>
#include <shared/LightData.h>
#include <shared/SphericalHarmonics.h>

struct SphereHit {
	vec3 normal;
};

hitAttributeNV SphereHit hit;

layout(location = 0) rayPayloadInNV vec3 hitValue;
layout(location = 1) rayPayloadNV bool inShadow;

layout(binding = 0, set = 0) uniform accelerationStructureNV topLevelAS;
layout(binding = 8, set = 0) uniform DirLightBlock { DirectionalLight dirLight; };

layout(set = 1, binding = 5) buffer readonly SphereSH { SphericalHarmonics SHs[]; } setSHs[];

bool hitPointInShadow(vec3 L)
{
	vec3 hitPoint = gl_WorldRayOriginNV + gl_HitTNV * gl_WorldRayDirectionNV;
	uint flags = gl_RayFlagsTerminateOnFirstHitNV | gl_RayFlagsSkipClosestHitShaderNV | gl_RayFlagsOpaqueNV;
	uint cullMask = 0xff;

	const int shadowPayloadIdx = 1;

	// Assume we are in shadow, and if the shadow miss shader activates we are *not* in shadow
	inShadow = true;

	traceNV(topLevelAS, flags, cullMask,
			0, // sbtRecordOffset
			0, // sbtRecordStride
			1, // missIndex
			hitPoint, 0.001, L, 1000.0,
			shadowPayloadIdx);

	return inShadow;
}

void main()
{
	vec3 N = normalize(hit.normal);

	SphericalHarmonics sh = setSHs[gl_InstanceCustomIndexNV].SHs[gl_PrimitiveID];
	vec3 baseColor = sampleSphericalHarmonic(sh, N);

	vec3 L = -normalize(dirLight.worldSpaceDirection.xyz);
	float shadowFactor = hitPointInShadow(L) ? 0.0 : 1.0;

	vec3 color = evaluateDirectionalLight(dirLight, baseColor, L, N, shadowFactor);

	hitValue = color;
}
