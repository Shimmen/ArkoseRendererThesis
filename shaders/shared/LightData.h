#ifndef LIGHT_DATA_H
#define LIGHT_DATA_H

struct DirectionalLight {
    vec4 colorAndIntensity;
    vec4 viewSpaceDirection; // (or maybe easier to do world space?)
    mat4 lightProjectionFromWorld;
};

#endif // LIGHT_DATA_H
