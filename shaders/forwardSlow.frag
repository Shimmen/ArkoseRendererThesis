#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "shared/ForwardData.h"

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vPosition;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in mat3 vTbnMatrix;

layout(set = 1, binding = 1) uniform sampler2D uBaseColor;
layout(set = 1, binding = 2) uniform sampler2D uNormalMap;

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oNormal;

void main()
{
    vec3 baseColor = texture(uBaseColor, vTexCoord).rgb;

    vec3 packedNormal = texture(uNormalMap, vTexCoord).rgb;
    vec3 mappedNormal = normalize(packedNormal * 2.0 - 1.0);
    vec3 N = normalize(vTbnMatrix * mappedNormal);

    oColor = vec4(baseColor, 1.0);
    oNormal = vec4(N * 0.5 + 0.5, 0.0);
}
