#include "input/input.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "core/geometry.h"
#include "ui/alert.h"


void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// Scroll arrives via callback (not polling); accumulate and drain once per frame.
static double g_scrollY = 0.0;
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    g_scrollY += yoffset;
}

static glm::vec3 snapToCardinal(glm::vec3 v) {
    float ax = std::abs(v.x), ay = std::abs(v.y), az = std::abs(v.z);
    if (ax >= ay && ax >= az)
        return v.x > 0 ? glm::vec3(1,0,0) : glm::vec3(-1,0,0);
    if (ay >= az)
        return v.y > 0 ? glm::vec3(0,1,0) : glm::vec3(0,-1,0);
    return v.z > 0 ? glm::vec3(0,0,1) : glm::vec3(0,0,-1);
}

static int findMatchingRotation(const glm::mat4& m) {
    for (int i = 0; i < 24; i++) {
        bool match = true;
        for (int c = 0; c < 3 && match; c++)
            for (int r = 0; r < 3 && match; r++)
                if (std::abs(m[c][r] - blockRotations[i][c][r]) > 0.01f)
                    match = false;
        if (match) return i;
    }
    return 0;
}

void handleInputs(GLFWwindow* window, glm::dvec3& cameraPos, double velocity,
                  const glm::vec3& cameraFront, const glm::vec3& cameraUp,
                  BuildInput& build, AlertSystem& alerts, BlockType& blockSelected) {
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        velocity *= 100.0;

    // Two layers of suppression: with a menu up the world-facing bindings are off; while the
    // menu is capturing a letter, every letter is data and nothing else fires.
    const bool menuUp  = build.menuOpen;
    const bool capture = build.keyCapture;

    // Raw letter scan (edge-triggered). Tracked every frame so edges stay correct, delivered
    // only while capturing.
    static bool letterWas[26] = {};
    build.letterKey = 0;
    for (int i = 0; i < 26; ++i) {
        bool d = glfwGetKey(window, GLFW_KEY_A + i) == GLFW_PRESS;
        if (d && !letterWas[i] && capture && build.letterKey == 0) build.letterKey = (char)('A' + i);
        letterWas[i] = d;
    }
    static bool bsWasDown = false;
    bool bsDown = glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
    build.backspaceKey = capture && bsDown && !bsWasDown;
    bsWasDown = bsDown;

    static bool bWasDown = false, vWasDown = false, nWasDown = false, mWasDown = false;
    static bool eWasDown = false, escWasDown = false, iWasDown = false;

    bool bDown = glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS;
    if (bDown && !bWasDown && !menuUp) {
        build.buildMode = !build.buildMode;
        if (!build.buildMode) build.orbitMode = build.blueprintMode = build.testMode = false;
        alerts.show(build.buildMode ? "Build Mode ON" : "Build Mode OFF");
    }
    bWasDown = bDown;

    bool nDown = glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS;
    if (nDown && !nWasDown && !menuUp && build.buildMode) {
        build.blueprintMode = !build.blueprintMode;
        build.orbitMode = false;   // an orbit anchor doesn't survive the world switch
        build.testMode = false;    // the room always opens in Building
        alerts.show(build.blueprintMode ? "Blueprint Mode ON" : "Blueprint Mode OFF");
    }
    nWasDown = nDown;

    // Room only: flip between designing the component and watching it run.
    bool mDown = glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS;
    if (mDown && !mWasDown && !menuUp && build.buildMode && build.blueprintMode) {
        build.testMode = !build.testMode;
        build.orbitMode = false;   // orbit is a placement tool
        alerts.show(build.testMode ? "Testing Mode" : "Building Mode");
    }
    mWasDown = mDown;

    // In the room, Testing means no placing/breaking: the build tools switch off together.
    bool testing  = build.blueprintMode && build.testMode;
    bool building = build.buildMode && !testing;

    bool vDown = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
    if (vDown && !vWasDown && !menuUp && building) {
        build.orbitMode = !build.orbitMode;
        if (build.orbitMode) {
            // Anchor the orbit on the current ghost block, or a spot just ahead if none.
            build.buildCursor = build.ghostValid ? build.ghostPos
                : glm::ivec3(glm::round(cameraPos + glm::dvec3(cameraFront) * 5.0));
            build.orbitDistance = std::clamp(
                glm::length(glm::dvec3(build.buildCursor) - cameraPos), 2.0, 60.0);
        }
        alerts.show(build.orbitMode ? "Orbit Placement" : "Free Placement");
    }
    vWasDown = vDown;

    bool rotating = building && !menuUp && glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
    bool orbiting = building && build.orbitMode;

    // Menu requests: E toggles (not while Tab-rotating, where it rolls the block), Esc
    // (without Ctrl, which quits) backs out, I is the debug item drop.
    bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
    bool eDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
    bool escDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    bool iDown = glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS;
    build.interact   = eDown && !eWasDown && !capture && !rotating;
    build.closeMenu  = escDown && !escWasDown && !ctrl;
    build.insertItem = iDown && !iWasDown && !capture;
    eWasDown = eDown; escWasDown = escDown; iWasDown = iDown;

    glm::vec3 front = glm::normalize(cameraFront);
    glm::vec3 up    = glm::normalize(cameraUp);
    glm::vec3 right = glm::normalize(glm::cross(front, up));

    enum { W, S, A, D, Q, E, SPACE, ALT };
    static bool wasDown[8] = {};
    bool down[8] = {
        glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS,
        glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS,
        glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS,
        glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS,
        glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS,
        glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS,
        glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS,
        glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS,
    };
    auto pressed = [&](int k) { return down[k] && !wasDown[k]; };

    if (menuUp) {
        // A menu is up: the camera and cursor hold still until it closes.
    } else if (rotating) {
        // Tab held: movement frozen, QWEASD spins the block about camera-snapped axes.
        glm::vec3 pitchAxis = snapToCardinal(right);
        glm::vec3 yawAxis   = snapToCardinal(up);
        glm::vec3 rollAxis  = snapToCardinal(front);

        auto tryRotate = [&](glm::vec3 axis, float angle) {
            glm::mat4 rot = glm::rotate(glm::mat4(1.0f), angle, axis);
            glm::mat4 current = blockRotations[build.blockRotation % 24];
            build.blockRotation = findMatchingRotation(rot * current);
        };

        if (pressed(W)) tryRotate(pitchAxis, glm::radians(-90.0f));
        if (pressed(S)) tryRotate(pitchAxis, glm::radians( 90.0f));
        if (pressed(A)) tryRotate(yawAxis,   glm::radians( 90.0f));
        if (pressed(D)) tryRotate(yawAxis,   glm::radians(-90.0f));
        if (pressed(Q)) tryRotate(rollAxis,  glm::radians( 90.0f));
        if (pressed(E)) tryRotate(rollAxis,  glm::radians(-90.0f));
    } else if (orbiting) {
        // Orbit Placement: WSAD steps the cursor block one voxel along the same
        // camera-snapped axes the rotation uses (the camera follows, being anchored);
        // Space/Alt step it up/down, matching the normal fly controls.
        glm::ivec3 fwd = glm::ivec3(glm::round(snapToCardinal(front)));
        glm::ivec3 rgt = glm::ivec3(glm::round(snapToCardinal(right)));
        glm::ivec3 upv = glm::ivec3(glm::round(snapToCardinal(up)));

        if (pressed(W)) build.buildCursor += fwd;
        if (pressed(S)) build.buildCursor -= fwd;
        if (pressed(D)) build.buildCursor += rgt;
        if (pressed(A)) build.buildCursor -= rgt;
        if (pressed(SPACE)) build.buildCursor += upv;
        if (pressed(ALT))   build.buildCursor -= upv;
    } else {
        glm::dvec3 dfront(front), dright(right);
        if (down[W]) cameraPos += velocity * dfront;
        if (down[S]) cameraPos -= velocity * dfront;
        if (down[A]) cameraPos -= velocity * dright;
        if (down[D]) cameraPos += velocity * dright;
    }
    for (int k = 0; k < 8; ++k) wasDown[k] = down[k];

    // Scroll zooms the orbit; drained every frame so free-mode scrolling can't bank up.
    double scroll = g_scrollY;
    g_scrollY = 0.0;
    if (orbiting && !menuUp && scroll != 0.0)
        build.orbitDistance = std::clamp(
            build.orbitDistance * std::pow(0.85, scroll), 2.0, 60.0);

    if (!rotating && !orbiting && !menuUp) {
        if (down[SPACE]) cameraPos += velocity * glm::dvec3(cameraUp);
        if (down[ALT])   cameraPos -= velocity * glm::dvec3(cameraUp);
    }

    if (building && !menuUp) {
        static bool zWasPressed = false;
        static bool xWasPressed = false;

        bool zPressed = glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
        bool xPressed = glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS;

        if (zPressed && !zWasPressed) {
            blockSelected = static_cast<BlockType>(
                (static_cast<int>(blockSelected) + 1) % BLOCK_TYPE_COUNT
            );
        }

        if (xPressed && !xWasPressed) {
            blockSelected = static_cast<BlockType>(
                (static_cast<int>(blockSelected) - 1 + BLOCK_TYPE_COUNT) % BLOCK_TYPE_COUNT
            );
        }

        zWasPressed = zPressed;
        xWasPressed = xPressed;
    }

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}
