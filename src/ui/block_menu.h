#pragma once

#include <glm/glm.hpp>

#include "sim/entity.h"
#include "ui/widgets.h"   // MenuPointer, MenuKeys

class AlertSystem;
class TextRenderer;
class UIRenderer;
struct FactorySim;

// The block inspection panel: a flat #626262 rectangle listing a machine's slots and what
// sits in them -- belts show one slot per position on the belt (front first) with the item's
// travel progress, inventories show stacks with counts. Items are view-only; the settings
// row is clickable: a port's Item (+ opens the full item list, - means none), a port's ID
// letter (+ waits MENU_ID_CAPTURE_SECONDS for a key), and every block's ON/OFF switch.
//
// It is opened on a world position, not an entity pointer: the owner re-resolves the entity
// every frame (so the panel tracks the live simulation) and closes the menu if it's gone.
struct BlockMenu {
    bool open = false;
    glm::ivec3 pos{0};

    bool   pickerOpen = false;     // choosing a port's item from the full list
    bool   capturing  = false;     // waiting for a port's ID letter
    double captureStart = 0.0;

    void openAt(glm::ivec3 p) { open = true; pos = p; pickerOpen = capturing = false; }
    void close() { open = false; pickerOpen = capturing = false; }

    // Esc backs out one level: letter capture -> item picker -> the menu itself.
    void onEscape() {
        if (capturing) capturing = false;
        else if (pickerOpen) pickerOpen = false;
        else close();
    }
    bool capturingLetter() const { return open && capturing; }

    // One frame: draws the panel (or the item picker) plus the pointer, and applies any click
    // or captured key to `e`.
    void frame(UIRenderer& ui, TextRenderer& text, BlockEntity& e, FactorySim& sim,
               AlertSystem& alerts, unsigned int itemTexture, int fbW, int fbH,
               const MenuPointer& ptr, const MenuKeys& keys, double now);
};
