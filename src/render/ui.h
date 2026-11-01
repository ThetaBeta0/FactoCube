#pragma once

#include <glm/glm.hpp>

#include "logger.h"

// Screen-space rectangles for menus: a solid colour, or one tile of a 16x16 atlas. Pixel
// coordinates with a top-left origin and y down -- the same convention as TextRenderer, so
// text and panels can be laid out with the same numbers.
class UIRenderer {
public:
    bool init(Logger& logger);
    void destroy();

    // Switches to overlay state (no depth test, alpha blending) for the calls in between;
    // end() puts the scene's state back. Text can be drawn in between.
    void begin(int fbW, int fbH);
    void end();

    void rect(float x, float y, float w, float h, glm::vec4 color);
    void tile(float x, float y, float w, float h, glm::ivec2 tile, unsigned int atlasTexture,
              glm::vec4 tint = glm::vec4(1.0f));

private:
    void quad(float x, float y, float w, float h);

    unsigned int program = 0, vao = 0, vbo = 0;
    int uScreenLoc = -1, uRectLoc = -1, uColorLoc = -1, uUseTexLoc = -1;
    int uAtlasLoc = -1, uTileLoc = -1;
    int fbW = 1, fbH = 1;
    bool savedDepth = false, savedCull = false, savedBlend = false;
};
