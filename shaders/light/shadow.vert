#version 450

#include "shared/ShadowData.h"

layout(location = 0) in vec3 aPosition;

layout(push_constant) uniform PushConstants {
    mat4 lightProjectionFromWorld;
};

layout(set = 0, binding = 0) uniform TransformDataBlock
{
    mat4 transforms[SHADOW_MAX_OCCLUDERS];
};

void main()
{
    mat4 worldFromLocal = transforms[gl_InstanceIndex];
    gl_Position = lightProjectionFromWorld * worldFromLocal * vec4(aPosition, 1.0);
}
