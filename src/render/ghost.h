#pragma once

#include <glm/glm.hpp>

#include "logger.h"

// Draws the build-mode placement preview: one block rendered semi-transparent
// with a colour tint (green = ok, red = blocked), textured from the atlas and
// using the same logarithmic depth as the scene so it occludes correctly.
class GhostRenderer {
public:
    bool init(Logger& logger);
    void destroy();

    // posRelCam: the voxel centre relative to the camera (floating origin).
    // The atlas texture is expected bound to texture unit 0 (the scene leaves it there).
    void draw(int type, int rotation, const glm::vec3& posRelCam,
              const glm::mat4& view, const glm::mat4& projection,
              float logFcoef, float tileSize,
              const glm::vec3& tint, float alpha);

private:
    unsigned int program = 0;
    unsigned int vao = 0, vbo = 0, ebo = 0;
    int uViewLoc = -1, uProjLoc = -1, uModelLoc = -1, uOriginLoc = -1;
    int uLogFLoc = -1, uAtlasLoc = -1, uTileLoc = -1, uTintLoc = -1, uAlphaLoc = -1;
};
