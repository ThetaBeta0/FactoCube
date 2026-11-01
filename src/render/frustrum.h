#pragma once

#include <array>

#include <glm/glm.hpp>

// Frustum plane: Ax + By + Cz + D = 0
struct Plane {
    glm::vec3 normal; // (A, B, C)
    float d;          // D
};

std::array<Plane, 6> extractFrustumPlanes(const glm::mat4& vp);

bool isAABBInFrustum(const std::array<Plane, 6>& planes, const glm::vec3& min, const glm::vec3& max);