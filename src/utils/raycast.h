#pragma once

#include <glm/glm.hpp>

template <typename SolidFn>
static bool raycastVoxel(glm::dvec3 origin, glm::dvec3 dir, double maxDist, glm::ivec3& hitVoxel, glm::ivec3& prevVoxel, SolidFn isSolid) {
    glm::ivec3 v((int)std::floor(origin.x), (int)std::floor(origin.y), (int)std::floor(origin.z));
    glm::ivec3 step(dir.x > 0 ? 1 : (dir.x < 0 ? -1 : 0), dir.y > 0 ? 1 : (dir.y < 0 ? -1 : 0), dir.z > 0 ? 1 : (dir.z < 0 ? -1 : 0));
    const double INF = 1e30;
    glm::dvec3 tMax, tDelta;

    for (int a = 0; a < 3; ++a) {
        if (dir[a] == 0.0) {
            tMax[a] = INF;
            tDelta[a] = INF;
        } else {
            double inv = 1.0 / std::abs(dir[a]);
            double cell = std::floor(origin[a]);
            tMax[a] = ((dir[a] > 0.0) ? (cell + 1.0 - origin[a]) : (origin[a] - cell)) * inv;
            tDelta[a] = inv;
        }
    }

    prevVoxel = v;
    double t = 0.0;

    for (int i = 0; i < 2048; ++i) {
        if (isSolid(v.x, v.y, v.z)) {
            hitVoxel = v;
            return true;
        }
        prevVoxel = v;
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            v.x += step.x;
            t = tMax.x;
            tMax.x += tDelta.x;
        } else if (tMax.y < tMax.z) {
            v.y += step.y;
            t = tMax.y;
            tMax.y += tDelta.y;
        } else {
            v.z += step.z;
            t = tMax.z;
            tMax.z += tDelta.z;
        }
        if (t > maxDist) return false;
    }
    return false;
}