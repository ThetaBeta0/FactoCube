#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include <glm/glm.hpp>

#include "core/constants.h"
#include "core/item.h"
#include "render/text.h"
#include "render/ui.h"

// Small immediate-mode widget kit shared by every overlay (block menu, build menu, pause).
// Pixel coordinates, top-left origin. Draw calls and hit-tests happen together: a widget
// draws itself and reports whether the pointer clicked it this frame.

// Pointer state for one frame: framebuffer pixels and whether the left button was clicked.
struct MenuPointer {
    glm::vec2 pos{0.0f};
    bool clicked = false;
};

// Raw keys a menu may consume this frame (only delivered while it is capturing a letter).
struct MenuKeys {
    char letter = 0;          // 'A'-'Z' pressed this frame, else 0
    bool backspace = false;
};

namespace wg {

struct Rect {
    float x, y, w, h;
    bool contains(glm::vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
};

// Everything a draw helper needs for one frame: renderers, the pointer, and the sizes that
// scale with the framebuffer height (so panels read the same at any resolution).
struct Ctx {
    UIRenderer& ui;
    TextRenderer& text;
    unsigned int tex;          // the item / UI atlas
    int fbW, fbH;
    const MenuPointer& ptr;
    float u, S, pad, titleScale, bodyScale, titleH, bodyH;
};

inline Ctx makeCtx(UIRenderer& ui, TextRenderer& text, unsigned int itemTexture,
                   int fbW, int fbH, const MenuPointer& ptr) {
    const float u = std::clamp(fbH / 900.0f, 0.75f, 2.0f);
    return Ctx{ ui, text, itemTexture, fbW, fbH, ptr,
                u, MENU_SLOT_PX * u, MENU_PAD_PX * u, 3.0f * u, 2.0f * u,
                TextRenderer::lineHeight(3.0f * u), TextRenderer::lineHeight(2.0f * u) };
}

inline const glm::ivec2 kSlotTile  (ITEM_SLOT_TILE_X,   ITEM_SLOT_TILE_Y);
inline const glm::ivec2 kPlusTile  (ITEM_PLUS_TILE_X,   ITEM_PLUS_TILE_Y);
inline const glm::ivec2 kMinusTile (ITEM_MINUS_TILE_X,  ITEM_MINUS_TILE_Y);
inline const glm::ivec2 kCursorTile(ITEM_CURSOR_TILE_X, ITEM_CURSOR_TILE_Y);
inline const glm::ivec2 kNoTile(-1);

inline const glm::vec3 kWhite(1.0f), kDim(0.85f), kFaint(0.7f);
inline const glm::vec3 kGreen(0.45f, 1.0f, 0.55f), kRed(1.0f, 0.4f, 0.35f), kYellow(1.0f, 0.85f, 0.2f);
inline const glm::vec4 kHover(1.0f, 1.0f, 1.0f, 0.18f);
inline const glm::vec4 kSelected(0.45f, 1.0f, 0.55f, 0.9f);

inline glm::ivec2 iconOf(ItemId id) {
    const Item& it = itemInfo(id);
    return { (int)it.iconTileX, (int)it.iconTileY };
}

inline void shadowText(Ctx& c, const std::string& s, float x, float y, float scale, glm::vec3 col = kWhite) {
    c.text.drawText(s, x + 1.0f, y + 1.0f, scale, 0.0f, 0.0f, 0.0f, c.fbW, c.fbH);
    c.text.drawText(s, x, y, scale, col.r, col.g, col.b, c.fbW, c.fbH);
}

inline void centredText(Ctx& c, const std::string& s, Rect r, float scale, glm::vec3 col) {
    float w = c.text.textWidth(s, scale), h = TextRenderer::lineHeight(scale);
    shadowText(c, s, r.x + (r.w - w) * 0.5f, r.y + (r.h - h) * 0.5f, scale, col);
}

// A slot frame with a tile centred in it. `tileTex` picks the atlas the inner tile comes from
// (0 = the item atlas; pass the block atlas for block faces). Clickable frames highlight under
// the pointer; returns true on the frame the pointer clicks it.
inline bool slot(Ctx& c, Rect r, glm::ivec2 tile, bool clickable, unsigned int tileTex = 0) {
    c.ui.tile(r.x, r.y, r.w, r.h, kSlotTile, c.tex);
    if (tile.x >= 0) {
        float inset = r.w * 0.125f;
        c.ui.tile(r.x + inset, r.y + inset, r.w - 2.0f * inset, r.h - 2.0f * inset, tile,
                  tileTex ? tileTex : c.tex);
    }
    bool hover = clickable && r.contains(c.ptr.pos);
    if (hover) c.ui.rect(r.x, r.y, r.w, r.h, kHover);
    return hover && c.ptr.clicked;
}

// The #626262 panel with its darker border, at an explicit place or centred on screen.
inline Rect panelAt(Ctx& c, Rect r) {
    c.ui.rect(r.x - 4.0f * c.u, r.y - 4.0f * c.u, r.w + 8.0f * c.u, r.h + 8.0f * c.u, MENU_BORDER_COLOR);
    c.ui.rect(r.x, r.y, r.w, r.h, MENU_COLOR);
    return r;
}
inline Rect panel(Ctx& c, float w, float h) {
    return panelAt(c, Rect{ std::floor((c.fbW - w) * 0.5f), std::floor((c.fbH - h) * 0.5f), w, h });
}

// A labelled push button; returns true on the frame it is clicked.
inline bool button(Ctx& c, Rect r, const std::string& label) {
    panelAt(c, r);
    bool hover = r.contains(c.ptr.pos);
    if (hover) c.ui.rect(r.x, r.y, r.w, r.h, kHover);
    centredText(c, label, r, c.bodyScale * 1.3f, kWhite);
    return hover && c.ptr.clicked;
}

// The pointer, drawn last so it sits over everything; its top-left corner is the hotspot.
inline void cursor(Ctx& c) {
    c.ui.tile(c.ptr.pos.x, c.ptr.pos.y, MENU_CURSOR_PX * c.u, MENU_CURSOR_PX * c.u, kCursorTile, c.tex);
}

} // namespace wg
