#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 vTexCoord;

layout(binding = 0) uniform sampler2D uTexture;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 color = texture(uTexture, vTexCoord).rgb;
    color = pow(color, vec3(1.0 / 2.2));
    oColor = vec4(color, 1.0);
}
