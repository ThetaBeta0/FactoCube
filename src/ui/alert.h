#pragma once

#include <string>

#include <glm/glm.hpp>

class TextRenderer;

// One-slot on-screen alert: big outlined text centred horizontally, sitting
// midway between the top of the screen and its centre. A new show() replaces
// whatever is currently displayed. The last part of the lifetime fades out.
class AlertSystem {
public:
    void show(const std::string& text,
              float duration = 2.0f,                          // seconds on screen
              float scale = 6.0f,                             // font scale (stb_easy_font is ~7px tall at 1.0)
              glm::vec3 color = glm::vec3(1.0f, 0.85f, 0.1f), // default yellow
              glm::vec3 outline = glm::vec3(0.0f));           // outline colour

    // Call once per frame after the 3D scene; dt advances the alert's clock.
    void draw(TextRenderer& tr, int fbW, int fbH, float dt);

private:
    std::string text;
    float scale = 6.0f;
    float timeLeft = 0.0f;
    glm::vec3 color{1.0f};
    glm::vec3 outline{0.0f};
};
