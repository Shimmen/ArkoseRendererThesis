#include "FpsCamera.h"

#include "GlobalState.h"

void FpsCamera::update(const Input& input, const Extent2D& screenExtent, float dt)
{
    m_didModify = false;

    // Apply acceleration from input

    vec3 acceleration { 0.0f };

    vec2 controllerMovement = input.leftStick();
    bool usingController = length(controllerMovement) > 0.0f;
    acceleration += controllerMovement.x * mathkit::globalRight;
    acceleration += controllerMovement.y * mathkit::globalForward;

    if (input.isKeyDown(GLFW_KEY_W))
        acceleration += mathkit::globalForward;
    if (input.isKeyDown(GLFW_KEY_S))
        acceleration -= mathkit::globalForward;

    if (input.isKeyDown(GLFW_KEY_D))
        acceleration += mathkit::globalRight;
    if (input.isKeyDown(GLFW_KEY_A))
        acceleration -= mathkit::globalRight;

    if (input.isKeyDown(GLFW_KEY_SPACE))
        acceleration += mathkit::globalUp;
    if (input.isKeyDown(GLFW_KEY_LEFT_SHIFT))
        acceleration -= mathkit::globalUp;

    if (usingController) {
        m_velocity += mathkit::rotateWithQuaternion(acceleration, m_orientation);
    } else {
        if (mathkit::length2(acceleration) > 0.01f && !GlobalState::get().guiIsUsingTheKeyboard()) {
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
    }

    // Apply velocity to position

    float speed = length(m_velocity);
    if (speed > 0.0f) {
        speed = mathkit::clamp(speed, 0.0f, maxSpeed);
        m_velocity = normalize(m_velocity) * speed;

        m_position += m_velocity * dt;
        m_didModify = true;
    }

    // Calculate rotation velocity from input

    vec3 prevPitchYawRoll = m_pitchYawRoll;

    // Make rotations less sensitive when zoomed in
    float fovMultiplier = 0.2f + ((m_fieldOfView - minFieldOfView) / (maxFieldOfView - minFieldOfView)) * 0.8f;

    vec2 controllerRotation = 0.3f * input.rightStick();
    m_pitchYawRoll.x -= controllerRotation.x * fovMultiplier * dt;
    m_pitchYawRoll.y += controllerRotation.y * fovMultiplier * dt;

    if (input.isButtonDown(GLFW_MOUSE_BUTTON_2) && !GlobalState::get().guiIsUsingTheMouse()) {
        // Screen size independent but also aspect ratio dependent!
        vec2 mouseDelta = input.mouseDelta() / float(screenExtent.width());

        m_pitchYawRoll.x += -mouseDelta.x * rotationMultiplier * fovMultiplier * dt;
        m_pitchYawRoll.y += -mouseDelta.y * rotationMultiplier * fovMultiplier * dt;
    }

    // Calculate banking due to movement

    vec3 right = rotate(m_orientation, mathkit::globalRight);
    vec3 forward = rotate(m_orientation, mathkit::globalForward);

    if (speed > 0.0f) {
        auto direction = m_velocity / speed;
        float speedAlongRight = dot(direction, right) * speed;
        float signOrZeroSpeed = float(speedAlongRight > 0.0f) - float(speedAlongRight < 0.0f);
        float bankAmountSpeed = std::abs(speedAlongRight) / maxSpeed * 2.0f;

        float rotationAlongY = m_pitchYawRoll.x;
        float signOrZeroRotation = float(rotationAlongY > 0.0f) - float(rotationAlongY < 0.0f);
        float bankAmountRotation = mathkit::clamp(std::abs(rotationAlongY) * 100.0f, 0.0f, 3.0f);

        float targetBank = ((signOrZeroSpeed * bankAmountSpeed) + (signOrZeroRotation * bankAmountRotation)) * baselineBankAngle;
        m_pitchYawRoll.z = mathkit::mix(m_pitchYawRoll.z, targetBank, 1.0f - pow(0.35f, dt));
    }

    // Damp rotation continuously

    m_pitchYawRoll *= pow(rotationDampening, dt);

    if (length(m_pitchYawRoll - prevPitchYawRoll) > 1e-6f) {
        m_didModify = true;
    }

    // Apply rotation

    m_orientation = angleAxis(m_pitchYawRoll.y, right) * m_orientation;
    m_orientation = angleAxis(m_pitchYawRoll.x, vec3(0, 1, 0)) * m_orientation;
    m_bankingOrientation = mathkit::axisAngle(forward, m_pitchYawRoll.z);

    // Apply zoom

    if (!GlobalState::get().guiIsUsingTheMouse()) {
        m_targetFieldOfView += -input.scrollDelta() * zoomSensitivity;
        m_targetFieldOfView = mathkit::clamp(m_targetFieldOfView, minFieldOfView, maxFieldOfView);
    }
    float fov = mathkit::mix(m_fieldOfView, m_targetFieldOfView, 1.0f - pow(0.01f, dt));
    if (abs(fov - m_fieldOfView) > 1e-6f) {
        m_didModify = true;
    }
    m_fieldOfView = fov;

    // Create the view matrix

    auto preAdjustedUp = rotate(m_orientation, vec3(0, 1, 0));
    auto up = rotate(m_bankingOrientation, preAdjustedUp);

    vec3 target = m_position + forward;
    m_viewFromWorld = mathkit::lookAt(m_position, target, up);

    // Create the projection matrix

    float width = screenExtent.width();
    float height = screenExtent.height();
    float aspectRatio = (height > 1e-6f) ? (width / height) : 1.0f;
    m_projectionFromView = mathkit::perspective(m_fieldOfView, aspectRatio, zNear, 10000.0f);
}

bool FpsCamera::didModifyOnLastUpdate() const
{
    return m_didModify;
}

void FpsCamera::lookAt(const vec3& position, const vec3& target, const vec3& up)
{
    m_position = position;
    auto direction = normalize(target - position);
    m_orientation = mathkit::quatLookAt(direction, up);
    m_viewFromWorld = mathkit::lookAt(m_position, target, up);
}
