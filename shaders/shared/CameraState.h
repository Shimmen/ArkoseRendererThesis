#ifndef CAMERA_STATE_H
#define CAMERA_STATE_H

struct CameraState {
    mat4 projectionFromView;
    mat4 viewFromWorld;
};

#endif // CAMERA_STATE_H
