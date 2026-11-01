#pragma once

#include <glm/glm.hpp>

#include "logger.h"

// Draws the blueprint room: the inside of a (2*half+1)^3 box, every wall block
// showing one atlas tile, lit and depth-written like the terrain.
class RoomRenderer {
public:
    bool init(Logger& logger);
    void destroy();

    // centreRelCam: the box centre relative to the camera. The atlas is expected bound
    // to texture unit 0 (the scene pass leaves it there).
    void draw(int half, const glm::vec3& centreRelCam,
              const glm::mat4& view, const glm::mat4& projection,
              float logFcoef, float tileSize, glm::ivec2 tile,
              const glm::vec3& sunDir, const glm::vec3& sunColor, float ambient);

private:
    unsigned int program = 0;
    unsigned int vao = 0, vbo = 0, ebo = 0;
    int uViewLoc = -1, uProjLoc = -1, uCentreLoc = -1, uSizeLoc = -1, uLogFLoc = -1;
    int uAtlasLoc = -1, uTileLoc = -1, uSunDirLoc = -1, uSunColorLoc = -1, uAmbientLoc = -1;
};
