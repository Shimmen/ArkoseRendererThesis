#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vTexCoord;

layout(binding = 1) uniform sampler2D uSampler;

layout(location = 0) out vec4 oColor;

void main()
{
    //vec3 color = vColor;
    vec3 color = texture(uSampler, vTexCoord).rgb;

    oColor = vec4(color, 1.0);
}
