#pragma once

struct BlendState {
    bool enabled { false };
};

struct RenderState {
    BlendState blendState {};
};
