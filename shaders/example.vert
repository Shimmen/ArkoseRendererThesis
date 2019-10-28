#version 450

vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

layout(location = 0) out vec3 vColor;

void main()
{
    vColor = colors[gl_VertexIndex];

    vec2 position2d = positions[gl_VertexIndex];
    gl_Position = vec4(position2d, 0.0, 1.0);
}
