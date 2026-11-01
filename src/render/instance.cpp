#include "render/instance.h"

#include <cstddef>

#include <glm/gtc/type_ptr.hpp>

void InstanceVBO::upload(const std::map<InstanceKey, std::vector<InstanceData>>& instanceGroups) {
    std::vector<InstanceData> packed;
    drawGroups.clear();
    for (const auto& [key, instances] : instanceGroups) {
        if (instances.empty()) continue;
        drawGroups.push_back({ key.face, key.atlasOffset, (int)packed.size(), (int)instances.size() });
        packed.insert(packed.end(), instances.begin(), instances.end());
    }
    if (packed.empty()) return;
    if (instanceVBO == 0) glGenBuffers(1, &instanceVBO);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, packed.size() * sizeof(InstanceData), packed.data(), GL_STATIC_DRAW);
}

void InstanceVBO::draw() {
    if (instanceVBO == 0 || drawGroups.empty()) return;
    glUniform3fv(chunkOriginLoc, 1, glm::value_ptr(originRelCam));
    glUniform1f(tileSizeLoc, tileSize);
    glUniform1f(depthBiasLoc, depthBias);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    for (const auto& g : drawGroups) {
        glUniform2fv(atlasOffsetLoc, 1, glm::value_ptr(g.atlasOffset));
        const size_t base = (size_t)g.first * sizeof(InstanceData);
        for (int i = 0; i < 4; ++i) {
            glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                (void*)(base + offsetof(InstanceData, model) + i * sizeof(glm::vec4)));
            glEnableVertexAttribArray(2 + i);
            glVertexAttribDivisor(2 + i, 1);
        }
        glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
            (void*)(base + offsetof(InstanceData, atlasOffset)));
        glEnableVertexAttribArray(6);
        glVertexAttribDivisor(6, 1);
        glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT,
            (void*)(g.face * faceIndexOffset * sizeof(unsigned int)), g.count);
    }
}

void InstanceVBO::deleteBuffers() {
    if (instanceVBO) glDeleteBuffers(1, &instanceVBO);
    instanceVBO = 0;
    drawGroups.clear();
}
