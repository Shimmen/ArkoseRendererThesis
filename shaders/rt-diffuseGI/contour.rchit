#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include <shared/RTData.h>
#include <shared/LightData.h>
#include <brdf.glsl>
#include <lighting.glsl>

layout(set = 1, binding = 9, scalar) buffer readonly Colors { vec4 colors[]; };

struct ContourHit {
	vec3 normal;
	uint colorIndex;
};

hitAttributeNV ContourHit hit;

layout(location = 0) rayPayloadInNV vec3 hitValue;
layout(location = 1) rayPayloadNV bool inShadow;

layout(binding = 0, set = 0) uniform accelerationStructureNV topLevelAS;
layout(binding = 8, set = 0) uniform DirLightBlock { DirectionalLight dirLight; };

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

	vec3 baseColor = colors[hit.colorIndex].rgb;
	// TODO: Encode linear sRGB in proxy-gen so we don't need to spend shader time on it here!
	baseColor = pow(baseColor, vec3(2.2));

	vec3 L = -normalize(dirLight.worldSpaceDirection.xyz);
	float shadowFactor = hitPointInShadow(L) ? 0.0 : 1.0;

	vec3 color = evaluateDirectionalLight(dirLight, baseColor, L, N, shadowFactor);

	hitValue = color;
}
