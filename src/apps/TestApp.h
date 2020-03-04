#pragma once

#include "rendering/App.h"
#include "utility/FpsCamera.h"
#include "utility/Model.h"

class TestApp : public App {
public:
    void setup(RenderGraph&) override;
    void update(float elapsedTime, float deltaTime) override;

private:
    std::unique_ptr<Model> m_cornellBox {};
    std::unique_ptr<Model> m_palletStack {};
    std::unique_ptr<Model> m_boomBox {};
    Scene m_scene {};
};
