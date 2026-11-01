#pragma once

#include <string>

#include "logger.h"

class TextRenderer {
public:
    bool init(Logger& logger);
    void destroy();

    void drawText(const std::string& text, float x, float y, float scale, float r, float g, float b, int screenW, int screenH, float a = 1.0f);
    float textWidth(const std::string& text, float scale) const;
    static float lineHeight(float scale);

private:
    unsigned int program = 0;
    unsigned int vao = 0, vbo = 0, ebo = 0;
    int uScreenLoc = -1, uOriginLoc = -1, uScaleLoc = -1;
};