#ifndef LIGHT_DATA_H
#define LIGHT_DATA_H

struct DirectionalLight {
    vec4 colorAndIntensity;
    vec4 worldSpaceDirection;
    vec4 viewSpaceDirection;
    mat4 lightProjectionFromWorld;
};

struct SpotLightData {
    vec4 colorAndIntensity;
    vec4 worldSpacePosition;
    vec4 worldSpaceDirection;
    vec4 viewSpacePosition;
    vec4 viewSpaceDirection;
    mat4 lightProjectionFromWorld;
    float coneAngle;
};

#endif // LIGHT_DATA_H
