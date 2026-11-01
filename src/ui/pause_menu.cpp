#include "ui/pause_menu.h"

using namespace wg;

PauseAction PauseMenu::frame(UIRenderer& ui, TextRenderer& text, unsigned int itemTexture,
                             int fbW, int fbH, const MenuPointer& ptr,
                             const std::vector<std::string>& debugLines) const {
    Ctx c = makeCtx(ui, text, itemTexture, fbW, fbH, ptr);
    PauseAction action = PauseAction::None;

    ui.begin(fbW, fbH);
    ui.rect(0.0f, 0.0f, (float)fbW, (float)fbH, PAUSE_OVERLAY_COLOR);

    const float margin = 4.0f * c.pad;

    // Left column: the game menu.
    float x = margin, y = margin;
    const float headScale = c.titleScale * 1.6f;
    shadowText(c, "PAUSED", x, y, headScale);
    y += TextRenderer::lineHeight(headScale) + 2.0f * c.pad;

    const float bw = MENU_BUTTON_W * c.u, bh = MENU_BUTTON_H * c.u;
    if (button(c, Rect{ x, y, bw, bh }, "Resume"))    action = PauseAction::Resume;
    y += bh + c.pad;
    if (button(c, Rect{ x, y, bw, bh }, "Quit Game")) action = PauseAction::Quit;
    y += bh + 2.0f * c.pad;
    shadowText(c, "Esc: resume", x, y, c.bodyScale, kFaint);

    // Right column: debug readout, right-aligned against the edge.
    const float rx = fbW - margin;
    y = margin;
    shadowText(c, "DEBUG", rx - c.text.textWidth("DEBUG", c.titleScale), y, c.titleScale);
    y += c.titleH + c.pad;
    for (const std::string& line : debugLines) {
        shadowText(c, line, rx - c.text.textWidth(line, c.bodyScale), y, c.bodyScale, kDim);
        y += c.bodyH + 2.0f * c.u;
    }

    cursor(c);
    ui.end();
    return action;
}
