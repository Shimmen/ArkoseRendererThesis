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

vec3 evaluateSpotLight(SpotLightData light, vec3 worldSpacePosition, vec3 baseColor, vec3 N, float shadowFactor)
{
    vec3 lightColor = light.colorAndIntensity.a * light.colorAndIntensity.rgb;

    vec3 pointToLight = light.worldSpacePosition.xyz - worldSpacePosition;
    float distToLight = length(pointToLight);
    vec3 L = pointToLight / distToLight;

    float distanceAttenuation = 1.0 / (distToLight * distToLight + 1e-4f);
    vec3 directLight = lightColor * shadowFactor * distanceAttenuation;
    if (dot(L, -light.worldSpaceDirection.xyz) < cos(0.5 * light.coneAngle)) {
        directLight = vec3(0.0);
    }

    float LdotN = max(dot(L, N), 0.0);
    return baseColor * diffuseBRDF() * LdotN * directLight;
}

#endif // LIGHTING_GLSL
