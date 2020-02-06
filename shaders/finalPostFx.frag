#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "aces.glsl"

layout(location = 0) in vec2 vTexCoord;

layout(binding = 0) uniform sampler2D uTexture;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 hdrColor = texture(uTexture, vTexCoord).rgb;

    vec3 ldrColor = ACES_tonemap(hdrColor);
    ldrColor = pow(ldrColor, vec3(1.0 / 2.2));

    oColor = vec4(ldrColor, 1.0);
}
