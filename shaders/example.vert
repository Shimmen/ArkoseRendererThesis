#version 450

#include "shared/camera_state.h"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

layout(binding = 0) uniform CameraStateBlock
{
    CameraState camera;
};

layout(location = 0) out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = camera.projection_from_local * vec4(aPosition, 1.0);
}
