#pragma once

#include <cmath>

#define GLM_FORCE_CXX17
#define GLM_FORCE_LEFT_HANDED

#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// (exposed for GLSL shader shared headers & convenience)
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat3 = glm::mat3;
using mat4 = glm::mat4;

namespace mathkit {

// NOTE: Everything in this namespace assumes a *left handed coordinate system*!

#include <glm/gtc/matrix_transform.hpp>

constexpr float PI = 3.14159265359f;
constexpr float TWO_PI = 2.0f * PI;

constexpr vec3 globalUp = vec3 { 0.0f, 1.0f, 0.0f };

constexpr float radians(float degrees)
{
    return degrees / 180.0f * PI;
}

inline float length2(const vec3& v)
{
    return glm::dot(v, v);
}

inline mat4 axisAngle(const vec3& axis, float angle)
{
    glm::quat quaternion = glm::angleAxis(angle, axis);
    mat4 matrix = mat4_cast(quaternion);
    return matrix;
}

inline mat4 lookAt(const vec3& eye, const vec3& target, const vec3& up = globalUp)
{
    return glm::lookAtLH(eye, target, up);
}

inline mat4 translate(float x, float y, float z)
{
    return glm::translate(mat4 { 1.0f }, { x, y, z });
}

inline mat4 infinitePerspective(float fieldOfViewY, float aspectRatio, float zNear)
{
    mat4 matrix = glm::infinitePerspectiveLH(fieldOfViewY, aspectRatio, zNear);
    matrix[1][1] *= -1.0f; // (flip for OpenGL -> Vulkan conventions)
    return matrix;
}

}