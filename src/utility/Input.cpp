#include "Input.h"

#include <cstring>

Input Input::s_instance {};

const Input& Input::instance()
{
    return s_instance;
}

void Input::registerWindow(GLFWwindow* window)
{
    Input& input = s_instance;
    void* userPtr = static_cast<void*>(&input);
    glfwSetWindowUserPointer(window, userPtr);

    glfwSetKeyCallback(window, Input::keyEventCallback);
    glfwSetMouseButtonCallback(window, Input::mouseButtonEventCallback);
    glfwSetCursorPosCallback(window, Input::mouseMovementEventCallback);
    glfwSetScrollCallback(window, Input::mouseScrollEventCallback);
}

void Input::preEventPoll()
{
    Input& input = s_instance;

    memset(input.m_wasKeyPressed, false, KEYBOARD_KEY_COUNT * sizeof(bool));
    memset(input.m_wasKeyReleased, false, KEYBOARD_KEY_COUNT * sizeof(bool));

    memset(input.m_wasButtonPressed, false, MOUSE_BUTTON_COUNT * sizeof(bool));
    memset(input.m_wasButtonReleased, false, MOUSE_BUTTON_COUNT * sizeof(bool));

    input.m_lastXPosition = input.m_currentXPosition;
    input.m_lastYPosition = input.m_currentYPosition;
    input.m_lastScrollOffset = input.m_currentScollOffset;
}

bool Input::isKeyDown(int key) const
{
    return m_isKeyDown[key];
}

bool Input::wasKeyPressed(int key) const
{
    return m_wasKeyPressed[key];
}

bool Input::wasKeyReleased(int key) const
{
    return m_wasKeyPressed[key];
}

bool Input::isButtonDown(int button) const
{
    return m_isButtonDown[button];
}

bool Input::wasButtonPressed(int button) const
{
    return m_wasButtonPressed[button];
}

bool Input::wasButtonReleased(int button) const
{
    return m_wasButtonReleased[button];
}

glm::vec2 Input::mousePosition(GLFWwindow* window) const
{
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    return vec2(float(x), float(y));
}

glm::vec2 Input::mouseDelta() const
{
    return vec2(m_currentXPosition - m_lastXPosition, m_currentYPosition - m_lastYPosition);
}

float Input::scrollDelta() const
{
    return float(m_currentScollOffset - m_lastScrollOffset);
}

void Input::keyEventCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto input = static_cast<Input*>(glfwGetWindowUserPointer(window));

    switch (action) {
    case GLFW_PRESS:
        input->m_wasKeyPressed[key] = true;
        input->m_isKeyDown[key] = true;
        break;

    case GLFW_RELEASE:
        input->m_wasKeyReleased[key] = true;
        input->m_isKeyDown[key] = false;
        break;

    case GLFW_REPEAT:
        // TODO: Handle repeat events!
    default:
        break;
    }
}

void Input::mouseButtonEventCallback(GLFWwindow* window, int button, int action, int mods)
{
    auto input = static_cast<Input*>(glfwGetWindowUserPointer(window));

    switch (action) {
    case GLFW_PRESS:
        input->m_wasButtonPressed[button] = true;
        input->m_isButtonDown[button] = true;
        break;

    case GLFW_RELEASE:
        input->m_wasButtonReleased[button] = true;
        input->m_isButtonDown[button] = false;
        break;

    case GLFW_REPEAT:
        // TODO: Handle repeat events!
    default:
        break;
    }
}

void Input::mouseMovementEventCallback(GLFWwindow* window, double xPos, double yPos)
{
    auto input = static_cast<Input*>(glfwGetWindowUserPointer(window));

    input->m_currentXPosition = xPos;
    input->m_currentYPosition = yPos;

    if (input->m_lastXPosition == -1.0) {
        input->m_lastXPosition = xPos;
        input->m_lastYPosition = yPos;
    }
}

void Input::mouseScrollEventCallback(GLFWwindow* window, double xOffset, double yOffset)
{
    auto input = static_cast<Input*>(glfwGetWindowUserPointer(window));

    // Ignore x-offset for now...
    input->m_currentScollOffset += yOffset;
}