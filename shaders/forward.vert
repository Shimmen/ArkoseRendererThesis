#version 450

#include "shared/CameraState.h"
#include "shared/ForwardData.h"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
//layout(location = 2) in vec3 aNormal;
//layout(location = 3) in vec4 aTangent;

layout(binding = 0) uniform CameraStateBlock
{
    CameraState camera;
};

layout(binding = 1) uniform TransformBlock
{
    mat4 transforms[FORWARD_MAX_TRANSFORMS];
};

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vPosition;
layout(location = 2) flat out int vObjectIndex;

void main()
{
    vObjectIndex = gl_InstanceIndex;
    mat4 worldFromLocal = transforms[vObjectIndex];

    vec4 viewSpacePos = camera.viewFromWorld * worldFromLocal * vec4(aPosition, 1.0);
    vPosition = viewSpacePos.xyz;

    vTexCoord = aTexCoord;

    gl_Position = camera.projectionFromView * viewSpacePos;
}
