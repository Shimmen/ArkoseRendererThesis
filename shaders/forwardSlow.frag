#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "shared/CameraState.h"
#include "shared/ForwardData.h"
#include "brdf.glsl"

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vPosition;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in mat3 vTbnMatrix;

layout(set = 0, binding = 0) uniform CameraStateBlock
{
    CameraState camera;
};

layout(set = 1, binding = 1) uniform sampler2D uBaseColor;
layout(set = 1, binding = 2) uniform sampler2D uNormalMap;

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oNormal;

// TODO: Move to some general location!
vec3 evaluateDirectionalLight(vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 lightColor = 10.0 * vec3(1, 1, 1); // TODO: LIGHT_COLOR_HERE_PLEASE
    vec3 L = -mat3(camera.viewFromWorld) * normalize(vec3(-1.0, -1.0, 0.0));

    float shadowFactor = 1.0; // TODO: Implement shadows!

    vec3 directLight = lightColor * shadowFactor;

    vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
    float LdotN = max(dot(L, N), 0.0);

    return brdf * LdotN * directLight;
}

void main()
{
    vec3 baseColor = texture(uBaseColor, vTexCoord).rgb;

    vec3 packedNormal = texture(uNormalMap, vTexCoord).rgb;
    vec3 mappedNormal = normalize(packedNormal * 2.0 - 1.0);
    vec3 N = normalize(vTbnMatrix * mappedNormal);

    vec3 V = -normalize(vPosition);

    // TODO!
    float roughness = 0.8;
    float metallic = 0.0;

    vec3 color = vec3(0.0);

    // TODO: Evaluate all light that will have an effect on this pixel/tile/cluster
    color += evaluateDirectionalLight(V, N, baseColor, roughness, metallic);

    oColor = vec4(color, 1.0);
    oNormal = vec4(N * 0.5 + 0.5, 0.0);
}
