#include "ui/build_menu.h"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace wg;

void BuildMenu::frame(UIRenderer& ui, TextRenderer& text, unsigned int itemTexture,
                      unsigned int blockTexture, int fbW, int fbH, const MenuPointer& ptr,
                      BlockType& selected, int rotation) const {
    Ctx c = makeCtx(ui, text, itemTexture, fbW, fbH, ptr);

    const int n    = BLOCK_TYPE_COUNT - 1;          // everything but air
    const int cols = MENU_PICKER_COLS;
    const int rows = (n + cols - 1) / cols;
    const char* title = "Build   (E: close)";

    char status[96];
    std::snprintf(status, sizeof(status), "Selected: %s   rot %d", blockTable[selected].name, rotation);

    float w = std::max({ cols * c.S + (cols + 1) * c.pad,
                         c.text.textWidth(title, c.titleScale) + 2.0f * c.pad,
                         c.text.textWidth(status, c.bodyScale) + 2.0f * c.pad });
    float h = c.pad + c.titleH + c.pad + rows * (c.S + c.pad) + 2.0f * c.bodyH + c.pad;

    ui.begin(fbW, fbH);
    Rect p = panel(c, w, h);
    shadowText(c, title, p.x + c.pad, p.y + c.pad, c.titleScale);
    const float gy = p.y + c.pad + c.titleH + c.pad;

    std::string hovered;
    for (int i = 0; i < n; ++i) {
        BlockType t = static_cast<BlockType>(i + 1);
        Rect r{ p.x + c.pad + (i % cols) * (c.S + c.pad), gy + (i / cols) * (c.S + c.pad), c.S, c.S };
        if (r.contains(ptr.pos)) hovered = blockTable[t].name;
        if (t == selected)   // ring behind the frame marks the current choice
            ui.rect(r.x - 3.0f * c.u, r.y - 3.0f * c.u, r.w + 6.0f * c.u, r.h + 6.0f * c.u, kSelected);
        if (slot(c, r, blockTable[t].tiles[Top], true, blockTexture)) selected = t;
    }

    float ly = gy + rows * (c.S + c.pad);
    shadowText(c, status, p.x + c.pad, ly, c.bodyScale);
    shadowText(c, hovered.empty() ? "Click a block to select it" : hovered,
               p.x + c.pad, ly + c.bodyH, c.bodyScale, hovered.empty() ? kFaint : kWhite);

    cursor(c);
    ui.end();
}
