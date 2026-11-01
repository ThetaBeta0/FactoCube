#pragma once

#include <map>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "core/geometry.h"

struct InstanceData {
    glm::mat4 model;
    glm::vec2 atlasOffset;
};

struct InstanceKey {
    Face face;
    glm::vec2 atlasOffset;

    bool operator<(const InstanceKey& other) const {
        if (face != other.face) return face < other.face;
        if (atlasOffset.x != other.atlasOffset.x) return atlasOffset.x < other.atlasOffset.x;
        return atlasOffset.y < other.atlasOffset.y;
    }
};

struct DrawGroup {
    Face face;
    glm::vec2 atlasOffset;
    int first;
    int count;
};

struct InstanceVBO {
    unsigned int instanceVBO = 0;
    std::vector<DrawGroup> drawGroups;
    int atlasOffsetLoc;
    int tileSizeLoc;
    int chunkOriginLoc;
    int depthBiasLoc = -1;
    float tileSize;
    glm::vec3 originRelCam{0.0f};
    float depthBias = 0.0f;

    void upload(const std::map<InstanceKey, std::vector<InstanceData>>& instanceGroups);

    void draw();
    void deleteBuffers();
};