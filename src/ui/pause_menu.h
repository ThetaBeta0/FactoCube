#pragma once

#include <string>
#include <vector>

#include "ui/widgets.h"

class TextRenderer;
class UIRenderer;

enum class PauseAction { None, Resume, Quit };

// The pause screen (Esc): a translucent grey sheet over the frozen world, the game menu down
// the left (Resume / Quit for now) and a debug readout down the right. The owner stops the
// simulation and the world clock while it is open.
struct PauseMenu {
    bool open = false;

    PauseAction frame(UIRenderer& ui, TextRenderer& text, unsigned int itemTexture,
                      int fbW, int fbH, const MenuPointer& ptr,
                      const std::vector<std::string>& debugLines) const;
};
