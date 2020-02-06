#pragma once

#include "rendering/App.h"
#include "utility/FpsCamera.h"
#include "utility/Model.h"

class TestApp : public App {
public:
    void setup(ResourceManager& staticResources, RenderGraph&) override;
    void update(float elapsedTime, float deltaTime) override;

private:
    std::unique_ptr<Model> m_boomBox {};
    std::unique_ptr<Model> m_cube {};
    Scene m_scene {};
};
