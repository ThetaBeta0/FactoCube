#include "frustrum.h"

std::array<Plane, 6> extractFrustumPlanes(const glm::mat4& vp) {
    std::array<Plane, 6> planes;
    
    // Left plane
    planes[0].normal.x = vp[0][3] + vp[0][0];
    planes[0].normal.y = vp[1][3] + vp[1][0];
    planes[0].normal.z = vp[2][3] + vp[2][0];
    planes[0].d = vp[3][3] + vp[3][0];
    
    // Right plane
    planes[1].normal.x = vp[0][3] - vp[0][0];
    planes[1].normal.y = vp[1][3] - vp[1][0];
    planes[1].normal.z = vp[2][3] - vp[2][0];
    planes[1].d = vp[3][3] - vp[3][0];
    
    // Bottom plane
    planes[2].normal.x = vp[0][3] + vp[0][1];
    planes[2].normal.y = vp[1][3] + vp[1][1];
    planes[2].normal.z = vp[2][3] + vp[2][1];
    planes[2].d = vp[3][3] + vp[3][1];
    
    // Top plane
    planes[3].normal.x = vp[0][3] - vp[0][1];
    planes[3].normal.y = vp[1][3] - vp[1][1];
    planes[3].normal.z = vp[2][3] - vp[2][1];
    planes[3].d = vp[3][3] - vp[3][1];
    
    // Near plane
    planes[4].normal.x = vp[0][3] + vp[0][2];
    planes[4].normal.y = vp[1][3] + vp[1][2];
    planes[4].normal.z = vp[2][3] + vp[2][2];
    planes[4].d = vp[3][3] + vp[3][2];
    
    // Far plane
    planes[5].normal.x = vp[0][3] - vp[0][2];
    planes[5].normal.y = vp[1][3] - vp[1][2];
    planes[5].normal.z = vp[2][3] - vp[2][2];
    planes[5].d = vp[3][3] - vp[3][2];
    
    // Normalize planes for accurate distance tests
    for (auto& plane : planes) {
        float len = glm::length(plane.normal);
        if (len > 0.0001f) {
            plane.normal /= len;
            plane.d /= len;
        }
    }
    return planes;
}

// Returns: true if AABB is at least partially inside frustum
bool isAABBInFrustum(const std::array<Plane, 6>& planes,
                     const glm::vec3& min, const glm::vec3& max) {
    for (const auto& plane : planes) {
        // Select the "p-vertex" (most positive corner relative to plane normal)
        glm::vec3 pVertex = (plane.normal.x > 0) ? glm::vec3(max.x, 0, 0) : glm::vec3(min.x, 0, 0);
        pVertex.y = (plane.normal.y > 0) ? max.y : min.y;
        pVertex.z = (plane.normal.z > 0) ? max.z : min.z;
        
        // If p-vertex is behind plane, entire AABB is outside
        if (glm::dot(plane.normal, pVertex) + plane.d < 0) {
            return false;
        }
    }
    return true; // At least partially inside
}