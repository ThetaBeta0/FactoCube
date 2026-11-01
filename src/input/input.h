#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "core/block.h"

class AlertSystem;

// Build-mode state, owned by main and mutated by handleInputs each frame.
//
//   B          toggles build mode
//   Tab (held) freezes movement; QWEASD rotates the block (build mode)
//   V          toggles Orbit Placement (build mode): the camera orbits the cursor
//              block (mouse pans around it, scroll zooms); WSAD steps the block
//              along camera-relative axes and Space/Alt step it up/down
//   N          toggles Blueprint Mode (build mode): the world is swapped for an
//              empty room where a component is designed with the same controls
//   M          in the room: Building <-> Testing. Testing runs the simulation and
//              disables placing/breaking so the component can be inspected
//   E          opens/closes the block menu for the machine under the crosshair
//   Esc        backs out of the block menu (letter capture -> item list -> menu)
//   I          (debug) drops one Iron Ingot into the targeted / open machine
//   A-Z        while a port menu is waiting for an ID: the letter (Backspace clears it)
struct BuildInput {
    bool buildMode = false;
    bool orbitMode = false;      // Orbit Placement (only meaningful while buildMode)
    bool blueprintMode = false;  // Blueprint room instead of the world (build mode only)
    bool testMode = false;       // Room only: Testing instead of Building
    int  blockRotation = 0;      // index into blockRotations (0-23)
    glm::ivec3 buildCursor{0};   // orbit mode: voxel the ghost sits at / camera orbits
    double orbitDistance = 6.0;  // camera distance from the cursor (scroll zooms)

    // Edge-triggered requests; main acts on them the frame they are raised.
    bool interact = false;       // E
    bool closeMenu = false;      // Esc
    bool insertItem = false;     // I (debug)
    char letterKey = 0;          // 'A'-'Z' pressed this frame while keyCapture is on, else 0
    bool backspaceKey = false;   // Backspace, while keyCapture is on

    // Written by main every frame.
    bool ghostValid = false;     // read here to seed the cursor when orbit turns on
    glm::ivec3 ghostPos{0};
    bool menuOpen = false;       // a menu is up: movement / rotation / mode toggles freeze
    bool keyCapture = false;     // a menu wants a raw letter: every letter binding is off
};

void handleInputs(GLFWwindow* window, glm::dvec3& cameraPos, double velocity,
                  const glm::vec3& cameraFront, const glm::vec3& cameraUp,
                  BuildInput& build, AlertSystem& alerts, BlockType& blockSelected);

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

enum FacingDirection {
    None = 0,
    East,
    West,
    South,
    North,
    Up,
    Down
};

inline FacingDirection getCameraGeneralFacingDirection(glm::vec3 cameraFront)
{
    glm::vec3 dir = glm::normalize(cameraFront);

    float ax = std::abs(dir.x);
    float ay = std::abs(dir.y);
    float az = std::abs(dir.z);

    if (ax >= ay && ax >= az)
        return dir.x > 0 ? East : West;

    if (ay >= az)
        return dir.y > 0 ? Up : Down;

    return dir.z > 0 ? South : North;
}
