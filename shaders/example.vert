#version 450

#include "shared/camera_state.h"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;

layout(binding = 0) uniform CameraStateBlock
{
    CameraState camera;
};

layout(location = 0) out vec2 vTexCoord;

void main()
{
    vTexCoord = aTexCoord;
    gl_Position = camera.projectionFromLocal * vec4(aPosition, 1.0);
}
