#version 450

#include "shared/camera_state.h"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aTexCoord;

layout(binding = 0) uniform CameraStateBlock
{
    CameraState camera;
};

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec2 vTexCoord;

void main()
{
    vColor = aColor;
    vTexCoord = aTexCoord;
    gl_Position = camera.projection_from_local * vec4(aPosition, 1.0);
}
