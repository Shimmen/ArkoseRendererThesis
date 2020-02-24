#ifndef RTDATA_H
#define RTDATA_H

struct RTMaterial {
    int baseColor;
    int pad0, pad1, pad2;
};

struct RTVertex {
    vec3 position;
    vec3 normal;
    vec2 texCoord;
    float pad0; // TODO: Check if this is needed!
};

#endif // RTDATA_H
