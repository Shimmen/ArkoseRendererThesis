#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "shared/RTData.h"
#include "shared/LightData.h"

layout(location = 0) rayPayloadInNV vec3 hitValue;
hitAttributeNV vec3 attribs;

layout(location = 1) rayPayloadNV bool inShadow;

layout(binding = 0, set = 0) uniform accelerationStructureNV topLevelAS;
layout(binding = 7, set = 0) uniform DirLightBlock { DirectionalLight dirLight; };

layout(binding = 0, set = 1, scalar) buffer readonly Meshes   { RTMesh meshes[]; };
layout(binding = 1, set = 1, scalar) buffer readonly Vertices { RTVertex x[]; } vertices[];
layout(binding = 2, set = 1)         buffer readonly Indices  { uint idx[]; }  indices[];
layout(binding = 3, set = 1) uniform sampler2D baseColorSamplers[RT_MAX_TEXTURES];

void unpack(out RTMesh mesh, out RTVertex v0, out RTVertex v1, out RTVertex v2)
{
	mesh = meshes[gl_InstanceID];
	uint objId = mesh.objectId;
	
	ivec3 idx = ivec3(indices[objId].idx[3 * gl_PrimitiveID + 0],
					  indices[objId].idx[3 * gl_PrimitiveID + 1],
					  indices[objId].idx[3 * gl_PrimitiveID + 2]);

	v0 = vertices[objId].x[idx.x];
	v1 = vertices[objId].x[idx.y];
	v2 = vertices[objId].x[idx.z];
}

bool hitPointInShadow()
{
	vec3 L = -normalize(dirLight.worldSpaceDirection.xyz);

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
	RTMesh mesh;
	RTVertex v0, v1, v2;
	unpack(mesh, v0, v1, v2);

	const vec3 b = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

	vec3 N = normalize(v0.normal.xyz * b.x + v1.normal.xyz * b.y + v2.normal.xyz * b.z);
	vec2 uv = v0.texCoord.xy * b.x + v1.texCoord.xy * b.y + v2.texCoord.xy * b.z;

	vec3 baseColor = texture(baseColorSamplers[mesh.baseColor], uv).rgb;

	float shadowFactor = hitPointInShadow() ? 0.0 : 1.0;
	baseColor *= shadowFactor;

	//hitValue = N * 0.5 + 0.5;
	//hitValue = vec3(uv, 0.0);
	hitValue = baseColor;
}
