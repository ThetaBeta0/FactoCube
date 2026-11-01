#include "ui/alert.h"

#include <algorithm>

#include "render/text.h"

// Fade-out window at the end of an alert's lifetime, in seconds.
static constexpr float FADE_TIME = 0.3f;

void AlertSystem::show(const std::string& t, float duration, float s,
                       glm::vec3 c, glm::vec3 o) {
    text = t;
    scale = s;
    color = c;
    outline = o;
    timeLeft = duration;
}

void AlertSystem::draw(TextRenderer& tr, int fbW, int fbH, float dt) {
    if (timeLeft <= 0.0f) return;
    timeLeft -= dt;
    if (timeLeft <= 0.0f) return;

    float alpha = std::min(1.0f, timeLeft / FADE_TIME);

    float x = (fbW - tr.textWidth(text, scale)) * 0.5f;
    float y = fbH * 0.25f - TextRenderer::lineHeight(scale) * 0.5f;

    // Outline: the glyph redrawn at 8 offsets one font-pixel out, then the fill on top.
    float o = std::max(1.0f, scale);
    static const float off[8][2] = {
        {-1,-1}, {0,-1}, {1,-1}, {-1,0}, {1,0}, {-1,1}, {0,1}, {1,1} };
    for (const auto& d : off)
        tr.drawText(text, x + d[0] * o, y + d[1] * o, scale,
                    outline.r, outline.g, outline.b, fbW, fbH, alpha);
    tr.drawText(text, x, y, scale, color.r, color.g, color.b, fbW, fbH, alpha);
}
