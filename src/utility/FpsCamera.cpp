#include "FpsCamera.h"

void FpsCamera::update(const Input& input, const Extent2D& screenExtent, float dt)
{
    // Apply acceleration from input

    vec3 acceleration { 0.0f };

    if (input.isKeyDown(GLFW_KEY_W))
        acceleration.z += 1;
    if (input.isKeyDown(GLFW_KEY_S))
        acceleration.z -= 1;

    if (input.isKeyDown(GLFW_KEY_D))
        acceleration.x += 1;
    if (input.isKeyDown(GLFW_KEY_A))
        acceleration.x -= 1;

    if (input.isKeyDown(GLFW_KEY_SPACE))
        acceleration.y += 1;
    if (input.isKeyDown(GLFW_KEY_LEFT_SHIFT))
        acceleration.y -= 1;

    if (mathkit::length2(acceleration) > 0.01f /* && !GuiSystem::isUsingKeyboard()*/) {
        acceleration = normalize(acceleration) * (maxSpeed / timeToMaxSpeed) * dt;
        m_velocity += mathkit::rotateWithQuaternion(acceleration, m_orientation);
    } else {
        // If no input and movement to acceleration decelerate instead
        if (length2(m_velocity) < stopThreshold) {
            m_velocity = vec3(0.0f);
        } else {
            vec3 deaccel = -normalize(m_velocity) * (maxSpeed / timeFromMaxSpeed) * dt;
            m_velocity += deaccel;
        }
    }

    // Apply velocity to position

    float speed = length(m_velocity);
    if (speed > 0.0f) {
        speed = mathkit::clamp(speed, 0.0f, maxSpeed);
        m_velocity = normalize(m_velocity) * speed;

        m_position += m_velocity * dt;
    }

    // Calculate rotation velocity from input

    if (input.isButtonDown(GLFW_MOUSE_BUTTON_2) /*&& !GuiSystem::isUsingMouse()*/) {
        // Screen size independent but also aspect ratio dependent!
        vec2 mouseDelta = input.mouseDelta() / float(screenExtent.width());

        // Make rotations less sensitive when zoomed in
        float fovMultiplier = 0.2f + ((m_fieldOfView - minFieldOfView) / (maxFieldOfView - minFieldOfView)) * 0.8f;

        m_pitchYawRoll.x += mouseDelta.x * rotationMultiplier * fovMultiplier * dt;
        m_pitchYawRoll.y += mouseDelta.y * rotationMultiplier * fovMultiplier * dt;
    }

    // Calculate banking due to movement

    vec3 right = rotate(m_orientation, mathkit::globalX);
    vec3 forward = rotate(m_orientation, mathkit::globalZ);

    if (speed > 0.0f) {
        auto direction = m_velocity / speed;
        float speedAlongRight = dot(direction, right) * speed;
        float signOrZeroSpeed = float(speedAlongRight > 0.0f) - float(speedAlongRight < 0.0f);
        float bankAmountSpeed = std::abs(speedAlongRight) / maxSpeed * 2.0f;

        float rotationAlongY = m_pitchYawRoll.x;
        float signOrZeroRotation = float(rotationAlongY > 0.0f) - float(rotationAlongY < 0.0f);
        float bankAmountRotation = mathkit::clamp(std::abs(rotationAlongY) * 100.0f, 0.0f, 3.0f);

        float targetBank = ((signOrZeroSpeed * -bankAmountSpeed) + (signOrZeroRotation * -bankAmountRotation)) * baselineBankAngle;
        m_pitchYawRoll.z = mathkit::mix(m_pitchYawRoll.z, targetBank, 1.0f - pow(0.35f, dt));
    }

    // Damp rotation continuously

    m_pitchYawRoll *= pow(rotationDampening, dt);

    // Apply rotation

    m_orientation = angleAxis(m_pitchYawRoll.y, right) * m_orientation;
    m_orientation = angleAxis(m_pitchYawRoll.x, vec3(0, 1, 0)) * m_orientation;
    m_bankingOrientation = mathkit::axisAngle(forward, m_pitchYawRoll.z);

    // Apply zoom

    if (true /*!GuiSystem::isUsingMouse()*/) {
        m_targetFieldOfView += -input.scrollDelta() * zoomSensitivity;
        m_targetFieldOfView = mathkit::clamp(m_targetFieldOfView, minFieldOfView, maxFieldOfView);
    }
    m_fieldOfView = mathkit::mix(m_fieldOfView, m_targetFieldOfView, 1.0f - pow(0.01f, dt));

    // Create the view matrix

    auto preAdjustedUp = rotate(m_orientation, vec3(0, 1, 0));
    auto up = rotate(m_bankingOrientation, preAdjustedUp);

    vec3 target = m_position + forward;
    m_viewFromWorld = mathkit::lookAt(m_position, target, up);

    // Create the projection matrix

    float aspectRatio = float(screenExtent.width()) / float(screenExtent.height());
    m_projectionFromView = mathkit::infinitePerspective(m_fieldOfView, aspectRatio, zNear);
}

void FpsCamera::lookAt(const vec3& position, const vec3& target, const vec3& up)
{
    m_position = position;
    auto direction = normalize(target - position);
    m_orientation = mathkit::quatLookAt(direction, up);
    m_viewFromWorld = mathkit::lookAt(m_position, target, up);
}
