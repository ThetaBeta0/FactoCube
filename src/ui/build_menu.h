#pragma once

#include "core/block.h"
#include "ui/widgets.h"

class TextRenderer;
class UIRenderer;

// The build menu (E while building): every placeable block as a slot showing its top face,
// straight from the block atlas. Clicking a block selects it for placement; the menu stays
// open until E (or Esc) closes it, so several can be compared.
struct BuildMenu {
    bool open = false;

    void frame(UIRenderer& ui, TextRenderer& text, unsigned int itemTexture,
               unsigned int blockTexture, int fbW, int fbH, const MenuPointer& ptr,
               BlockType& selected, int rotation) const;
};
