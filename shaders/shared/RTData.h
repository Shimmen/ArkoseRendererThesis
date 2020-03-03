#ifndef RTDATA_H
#define RTDATA_H

#define RT_MAX_TEXTURES 256

struct RTMesh {
    int objectId;
    int baseColor;

    //int pad0, pad1;
};

struct RTVertex {
    // TODO: we could fit the tex coord data in .w of position & normal!
    vec4 position;
    vec4 normal;
    vec4 texCoord;
};

#endif // RTDATA_H
