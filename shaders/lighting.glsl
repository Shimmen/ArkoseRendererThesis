#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

#include <shared/LightData.h>
#include <brdf.glsl>
#include <common.glsl>

vec3 evaluateDirectionalLight(DirectionalLight light, vec3 baseColor, vec3 L, vec3 N, float shadowFactor)
{
    vec3 lightColor = light.colorAndIntensity.a * light.colorAndIntensity.rgb;

    vec3 directLight = lightColor * shadowFactor;
    float LdotN = max(dot(L, N), 0.0);

    return baseColor * diffuseBRDF() * LdotN * directLight;
}

#endif // LIGHTING_GLSL
