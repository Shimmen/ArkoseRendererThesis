#ifndef CAMERA_STATE_H
#define CAMERA_STATE_H

struct CameraState {
    mat4 projection_from_view;
    mat4 view_from_world;
    mat4 world_from_local;

    mat4 view_from_local;
    mat4 projection_from_local;
};

#endif // CAMERA_STATE_H
