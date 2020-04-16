#pragma once

#include "Extent.h"
#include "Input.h"
#include "mathkit.h"

class FpsCamera {
public:
    FpsCamera() = default;
    ~FpsCamera() = default;

    void setMaxSpeed(float);

    void update(const Input&, const Extent2D& screenExtent, float deltaTime);

    void setDidModify(bool);
    bool didModify() const;

    void lookAt(const vec3& position, const vec3& target, const vec3& up = mathkit::globalY);

    vec3 position() const { return m_position; }
    void setPosition(vec3 p) { m_position = p; }

    quat orientation() const { return m_orientation; }
    void setOrientation(quat q) { m_orientation = q; }

    [[nodiscard]] mat4 viewMatrix() const { return m_viewFromWorld; }
    [[nodiscard]] mat4 projectionMatrix() const { return m_projectionFromView; }

private:
    vec3 m_position {};
    vec3 m_velocity {};

    quat m_orientation {};
    vec3 m_pitchYawRoll {};
    quat m_bankingOrientation { 1, 0, 0, 0 };

    float m_fieldOfView { mathkit::radians(60.0f) };
    float m_targetFieldOfView { m_fieldOfView };

    mat4 m_viewFromWorld {};
    mat4 m_projectionFromView {};

    bool m_didModify { true };

    static constexpr float zNear { 0.25f };

    float maxSpeed { 10.0f };
    static constexpr float timeToMaxSpeed { 0.25f };
    static constexpr float timeFromMaxSpeed { 0.60f };
    static constexpr float stopThreshold { 0.02f };

    static constexpr float rotationMultiplier { 30.0f };
    static constexpr float rotationDampening { 0.000005f };

    static constexpr float zoomSensitivity { 0.15f };
    static constexpr float minFieldOfView { mathkit::radians(15.0f) };
    static constexpr float maxFieldOfView { mathkit::radians(60.0f) };

    static constexpr float baselineBankAngle { mathkit::radians(30.0f) };
};
