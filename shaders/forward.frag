#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "shared/ForwardData.h"

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vPosition;
layout(location = 2) flat in int vObjectIndex;

layout(binding = 2) uniform sampler2D uDiffuseSamplers[FORWARD_MAX_TEXTURES];

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 color = texture(uDiffuseSamplers[vObjectIndex], vTexCoord).rgb;
    oColor = vec4(color, 1.0);
}
