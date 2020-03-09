#pragma once

#include "rendering/App.h"
#include "utility/FpsCamera.h"
#include "utility/Model.h"
#include "utility/Scene.h"

class TestApp : public App {
public:
    void setup(RenderGraph&) override;
    void update(float elapsedTime, float deltaTime) override;

private:
    Model* m_cornellBox {};
    Model* m_palletStack {};
    Model* m_boomBox {};
    Scene m_scene {};
};
