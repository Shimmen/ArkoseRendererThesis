#pragma once

#include "rendering/App.h"
#include "utility/FpsCamera.h"
#include "utility/Model.h"

class TestApp : public App {
public:
    void setup(ResourceManager& staticResources, RenderGraph&) override;
    void update(float elapsedTime, float deltaTime) override;

private:
    std::unique_ptr<Model> m_model {};
    Scene m_scene {};

    FpsCamera m_camera {};
};
