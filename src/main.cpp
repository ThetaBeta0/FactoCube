#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <string>
// for std::clamp / std::shuffle
#include <algorithm>
#include <climits>
#include <random>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "logger.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb-master/stb_image.h>
#include <map>
#include <unordered_map>
#include <tuple>
#include <thread>
#include <queue>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <cstdio>

#include "core/block.h"
#include "core/constants.h"
#include "core/geometry.h"
#include "core/hash.h"

#include "input/input.h"

#include "jobs/chunk_job.h"

#include "world/biome.h"
#include "world/chunk.h"
#include "world/terrain.h"

#include "render/frustrum.h"
#include "render/text.h"
#include "render/ghost.h"
#include "render/room.h"
#include "render/shader.h"
#include "render/water.h"

#include "ui/alert.h"

#include "utils/raycast.h"

#include "construct/construct.h"

#include "sim/factory.h"
#include "sim/recipe.h"

#include "render/ui.h"
#include "ui/block_menu.h"
#include "ui/build_menu.h"
#include "ui/pause_menu.h"

int main(void)
{
    // Global queues (or pass by reference)
    TaskQueue generationQueue;
    TaskQueue completedQueue;

    // Start a pool of worker threads so chunk generation keeps up when moving fast.
    unsigned int hw = std::thread::hardware_concurrency();
    unsigned int numWorkers = std::min(8u, hw > 2 ? hw - 1 : 2u);
    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < numWorkers; ++i)
        workers.emplace_back(chunkWorkerThread, std::ref(generationQueue), std::ref(completedQueue));

    // Chunks are keyed by (lod, cx, cy, cz) so a world cell can exist at several LODs.
    using ChunkKey = std::tuple<int, int, int, int>;
    // Track pending chunks to avoid duplicate requests
    std::unordered_set<ChunkKey, tuple_hash> pendingChunks;

    Logger logger("log.txt");
    logger.logSection("PROGRAM START");

    // Recipes are data; a typo there (an output on a face with no Output port, say) is far
    // easier to spot in the log than as a machine that mysteriously never runs.
    if (std::string problems = validateRecipes(); !problems.empty())
        logger.logError("Recipe table problems:\n" + problems);
    
    if (!glfwInit())
    {
        logger.logError("Failed to initialize GLFW");
        return -1;
    }
    logger.logSuccess("GLFW initialized");
    
    GLFWwindow* window = glfwCreateWindow(800, 600, "Temp", nullptr, nullptr);
    if (!window)
    {
        logger.logError("Failed to create window");
        glfwTerminate();
        return -1;
    }
    logger.logSuccess("Window created (800x600)");
    
    glfwMakeContextCurrent(window);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    glfwSetWindowMonitor(
        window,
        monitor,
        0, 0,
        mode->width,
        mode->height,
        mode->refreshRate
    );
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetScrollCallback(window, scroll_callback);
    logger.log("Switched to fullscreen: " + std::to_string(mode->width) + "x" + std::to_string(mode->height));

    if (!window)
    { 
        logger.logError("Window is null after setup");
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        logger.logError("Failed to initialize OpenGL with GLAD");
        return -1;
    }
    logger.logSuccess("OpenGL initialized with GLAD");

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    unsigned int shaderProgram = createShaderProgram("shaders/vertex_shader.glsl", "shaders/fragment_shader.glsl", logger);
    if (shaderProgram == 0) return -1;

    unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");

    unsigned int VBO, VAO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Face_Indices), Face_Indices.data(), GL_STATIC_DRAW);

    glBufferData(GL_ARRAY_BUFFER, sizeof(Faces), Faces.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); // Position
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); // Texture
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Load texture
    unsigned int texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // The texture is a 16x16-tile atlas with 16px tiles. Mip level 4 collapses each tile to a
    // single texel (its own average); level 5+ would average ADJACENT tiles together, so a
    // minified block reads as a muddy mix of its neighbours (e.g. lava -> grey/green/black).
    // Cap the mip chain at level 4 to keep distance minification but never bleed across tiles.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4);

    
    // Load image file
    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);  // Flip Y-axis for OpenGL
    unsigned char* data = stbi_load("FactoCubeTextureAtlas.png", &width, &height, &nrChannels, 0);
    
    if (data) {
        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        logger.log("Texture loaded: " + std::to_string(width) + "x" + std::to_string(height));
        stbi_image_free(data);
    } else {
        logger.logError("Failed to load FactoCubeTextureAtlas.png");
    }

    // Item icon atlas (16x16 tiles) for menus: (0,0) is the error tile, (0,15) the slot frame.
    // Drawn only at UI scale, so plain nearest sampling and no mip chain.
    unsigned int itemTexture;
    glGenTextures(1, &itemTexture);
    glBindTexture(GL_TEXTURE_2D, itemTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    {
        int iw, ih, ich;
        unsigned char* ipx = stbi_load("FactoCubeItemTextures.png", &iw, &ih, &ich, 4);
        if (ipx) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, iw, ih, 0, GL_RGBA, GL_UNSIGNED_BYTE, ipx);
            logger.log("Item texture loaded: " + std::to_string(iw) + "x" + std::to_string(ih));
            stbi_image_free(ipx);
        } else {
            logger.logError("Failed to load FactoCubeItemTextures.png");
        }
    }

    TextRenderer textRenderer;
    textRenderer.init(logger);

    UIRenderer ui;
    if (!ui.init(logger)) return -1;

    AlertSystem alerts;

    GhostRenderer ghostRenderer;
    ghostRenderer.init(logger);

    RoomRenderer roomRenderer;
    if (!roomRenderer.init(logger)) return -1;

    WaterRenderer waterRenderer;
    if (!waterRenderer.init(logger)) return -1;

    // Water chunks visible this frame, drawn as a separate pass after the opaque terrain.
    struct WaterDraw { Chunk* chunk; glm::vec2 relXZ; };
    std::vector<WaterDraw> waterDraws;

    int windowWidth = mode->width;
    int windowHeight = mode->height;
    

    float cameraSpeed = 2.5f;
    // glm::dvec3 cameraPos = glm::dvec3(1149 * (int)(40075017 / 4320), 1000,  719 * (int)(20037508 / 2160));
    glm::dvec3 cameraPos = glm::dvec3(10658468, 22, 6677749);
    glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f,  0.0f);

    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

    glm::mat4 projection = glm::perspective(
        glm::radians(80.0f),
        (float)windowWidth / (float)windowHeight,
        NEAR_PLANE,
        FAR_PLANE
    );
    const float logFcoef = 2.0f / std::log2(FAR_PLANE + 1.0f);

    unsigned int projLoc = glGetUniformLocation(shaderProgram, "projection");
    unsigned int viewLoc = glGetUniformLocation(shaderProgram, "view");


    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    double xpos, ypos;
    float yaw   = -90.0f; // looking down -Z
    float pitch =  0.0f;

    float sensitivity = 0.1f;
    bool firstMouse = true;
    float lastX, lastY;

    float tileSize = 1.0f / 16.0f;

    int atlasOffsetLoc = glGetUniformLocation(shaderProgram, "atlasOffset");
    int tileSizeLoc = glGetUniformLocation(shaderProgram, "tileSize");
    int chunkOriginLoc = glGetUniformLocation(shaderProgram, "chunkOrigin");
    int depthBiasLoc = glGetUniformLocation(shaderProgram, "depthBias");

    int logFcoefLoc  = glGetUniformLocation(shaderProgram, "logFcoef");
    int sunDirLoc    = glGetUniformLocation(shaderProgram, "sunDir");
    int sunColorLoc  = glGetUniformLocation(shaderProgram, "sunColor");
    int ambientLoc   = glGetUniformLocation(shaderProgram, "ambient");
    int fogColorLoc  = glGetUniformLocation(shaderProgram, "fogColor");
    int fogStartLoc  = glGetUniformLocation(shaderProgram, "fogStart");
    int fogEndLoc    = glGetUniformLocation(shaderProgram, "fogEnd");

    const glm::vec3 sunDir   = glm::normalize(glm::vec3(0.35f, 0.85f, 0.40f));
    const glm::vec3 sunColor = glm::vec3(0.62f, 0.60f, 0.55f);
    const float     ambient  = 0.45f;
    const glm::vec3 fogColor = glm::vec3(0.62f, 0.72f, 0.85f);

    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    float lastTime = 0.0f;
    int frameCount = 0;
    int chunkCount = 0;
    int displayFps = 0;

    int heightmapWidth, heightmapHeight;
    unsigned char* heightMapData = loadHeightMap("heightMap.png", heightmapWidth, heightmapHeight);

    int biomeW, biomeH, biomeCh;
    unsigned char* biomeMapData = stbi_load("biomeMap.png", &biomeW, &biomeH, &biomeCh, 3);
    if (biomeMapData) {
        g_biomeMap = biomeMapData; g_biomeMapW = biomeW; g_biomeMapH = biomeH;
        logger.log("Biome map loaded: " + std::to_string(biomeW) + "x" + std::to_string(biomeH));
    } else {
        logger.logError("Failed to load biomeMap.png; using the default biome everywhere");
    }

    std::unordered_map<ChunkKey, Chunk, tuple_hash> loadedChunks;

    // Free-floating editable objects. Edits attach to the nearest one within
    // CONSTRUCT_MERGE_DISTANCE; otherwise a new construct is appended here.
    std::vector<Construct> constructs;

    bool hasLeftDown = false;
    bool hasRightDown = false;

    auto floorDiv = [](int a, int b) {
        int q = a / b, r = a % b;
        return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
    };

    auto isSolid = [&](int wx, int wy, int wz) -> bool {
        glm::ivec3 wp(wx, wy, wz);
        // Constructs win over terrain so the ray hits (and edits act on) them first.
        for (const auto& c : constructs)
            if (c.isSolidAt(wp)) return true;
        int cx = floorDiv(wx, CHUNK_SIZE), cy = floorDiv(wy, CHUNK_SIZE), cz = floorDiv(wz, CHUNK_SIZE);
        auto it = loadedChunks.find(ChunkKey{0, cx, cy, cz});
        if (it != loadedChunks.end()) return it->second.blockData[wx - cx * CHUNK_SIZE][wy - cy * CHUNK_SIZE][wz - cz * CHUNK_SIZE].type != AIR;
        return false;
    };

    // Wire the shared render uniforms onto a construct's VBO, then (re)mesh + upload it.
    auto rebuildConstruct = [&](Construct& c) {
        c.instanceVBO.atlasOffsetLoc = atlasOffsetLoc;
        c.instanceVBO.tileSizeLoc    = tileSizeLoc;
        c.instanceVBO.chunkOriginLoc = chunkOriginLoc;
        c.instanceVBO.depthBiasLoc   = depthBiasLoc;
        c.instanceVBO.tileSize       = tileSize;
        c.rebuild();
    };

    // Place a block: attach to the nearest construct within CONSTRUCT_MERGE_DISTANCE,
    // otherwise start a new construct at this position.
    auto placeBlockAt = [&](glm::ivec3 wv, int type, int rotation) {
        Block b = makeBlock(type, rotation);
        Construct* best = nullptr;
        int bestDist = INT_MAX;
        for (auto& c : constructs) {
            int d = c.distanceTo(wv);
            if (d < bestDist) { bestDist = d; best = &c; }
        }
        if (best && bestDist <= CONSTRUCT_MERGE_DISTANCE) {
            best->setBlock(wv, b);
            rebuildConstruct(*best);
        } else {
            constructs.emplace_back();
            Construct& c = constructs.back();
            c.setBlock(wv, b);
            rebuildConstruct(c);
        }
    };

    // Break a block: only constructs are editable, so find the one holding it.
    auto breakBlockAt = [&](glm::ivec3 wv) {
        for (auto& c : constructs) {
            if (c.isSolidAt(wv)) {
                c.setBlock(wv, Block{});
                rebuildConstruct(c);
                return;
            }
        }
    };

    // ---- Blueprint mode: a component designed in an empty room instead of the world --
    // Room-local coordinates (centre = origin); the camera and all edits switch to this
    // space while build.blueprintMode is on. The other mode's camera is parked in altCam*
    // and swapped back on the way out, so both views resume where they were left.
    Construct blueprint;
    int  blueprintVolume = 0;               // non-air blocks in `blueprint`
    bool inBlueprint = false;               // main's copy of build.blueprintMode (detects the switch)
    glm::dvec3 altCamPos(0.0, 0.0, 8.0);    // the OTHER mode's camera; starts as the room spawn
    float altYaw = -90.0f, altPitch = 0.0f;

    auto frontFrom = [](float yawDeg, float pitchDeg) {
        return glm::normalize(glm::vec3(
            cos(glm::radians(yawDeg)) * cos(glm::radians(pitchDeg)),
            sin(glm::radians(pitchDeg)),
            sin(glm::radians(yawDeg)) * cos(glm::radians(pitchDeg))));
    };

    // Everything outside the room is wall; inside, only the blueprint's own blocks count.
    auto isSolidBP = [&](int x, int y, int z) -> bool {
        if (std::abs(x) > BLUEPRINT_HALF || std::abs(y) > BLUEPRINT_HALF || std::abs(z) > BLUEPRINT_HALF)
            return true;
        return blueprint.isSolidAt(glm::ivec3(x, y, z));
    };
    auto solidAt = [&](glm::ivec3 p) {
        return inBlueprint ? isSolidBP(p.x, p.y, p.z) : isSolid(p.x, p.y, p.z);
    };
    auto bpPlace = [&](glm::ivec3 p, int type, int rotation) {
        blueprint.setBlock(p, makeBlock(type, rotation));
        rebuildConstruct(blueprint);
        ++blueprintVolume;
    };
    auto bpBreak = [&](glm::ivec3 p) {
        if (!blueprint.isSolidAt(p)) return;
        blueprint.setBlock(p, Block{});
        rebuildConstruct(blueprint);
        --blueprintVolume;
    };

    BuildInput build;

    BlockType blockSelected = CONVEYER_BELT;

    // Item flow between machines (belts, barrels, splitters...). Runs in fixed steps over
    // whichever constructs the current mode is showing.
    FactorySim sim;

    // The block inspection panel (E), and the room mode it was opened in.
    BlockMenu menu;
    bool lastTesting = false;
    bool cursorFree = false;   // pointer released for a menu (else captured for mouse look)
    BuildMenu buildMenu;       // E while building: pick the block to place
    PauseMenu pause;           // Esc: sheet over the world, game menu left, debug readout right
    float gameTime = 0.0f;     // world clock for animations; stands still while paused

    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        if (!pause.open) gameTime += deltaTime;
        frameCount++;
        if (currentFrame - lastTime >= 1.0f) {
            float fps = frameCount;
            displayFps = frameCount;
            frameCount = 0;
            lastTime = currentFrame;
        }
        chunkCount = 0;

        glfwPollEvents();

        double velocity = (double)cameraSpeed * deltaTime;

        int fbW, fbH, winW, winH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glfwGetWindowSize(window, &winW, &winH);

        // Any overlay frees the pointer (hidden -- the overlay draws its own arrow, starting
        // from the centre); closing it captures the pointer again for mouse look. The look
        // delta is reset across the switch so the view doesn't jump.
        bool anyMenu = menu.open || buildMenu.open || pause.open;
        if (anyMenu != cursorFree) {
            cursorFree = anyMenu;
            glfwSetInputMode(window, GLFW_CURSOR, cursorFree ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_DISABLED);
            if (cursorFree) glfwSetCursorPos(window, winW * 0.5, winH * 0.5);
            firstMouse = true;
        }

        // Mouse look runs before input so orbit mode can anchor the camera this frame.
        glfwGetCursorPos(window, &xpos, &ypos);
        glm::vec2 pointerPx((float)(xpos * fbW / winW), (float)(ypos * fbH / winH));   // framebuffer px

        if (firstMouse)
        {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
        }

        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos; // reversed (Y goes down)

        lastX = xpos;
        lastY = ypos;

        xoffset *= sensitivity;
        yoffset *= sensitivity;

        if (!anyMenu) {   // the view holds still while an overlay is up
            yaw   += xoffset;
            pitch += yoffset;
        }

        // Clamp BEFORE trig
        pitch = std::clamp(pitch, -89.0f, 89.0f);

        cameraFront = frontFrom(yaw, pitch);

        build.menuOpen = anyMenu;
        build.keyCapture = menu.capturingLetter();
        handleInputs(window, cameraPos, velocity, cameraFront, cameraUp, build, alerts, blockSelected);

        // Entering/leaving the blueprint room: park this mode's camera, pick up the
        // other's, and re-derive the look direction for the rest of this frame.
        if (build.blueprintMode != inBlueprint) {
            inBlueprint = build.blueprintMode;
            std::swap(cameraPos, altCamPos);
            std::swap(yaw, altYaw);
            std::swap(pitch, altPitch);
            cameraFront = frontFrom(yaw, pitch);
            build.ghostValid = false;
            menu.close();
        }

        // Room modes: Testing runs the simulation and turns the build tools off, so a
        // component can be watched and inspected without accidentally editing it.
        bool testing  = inBlueprint && build.testMode;
        bool building = build.buildMode && !testing;     // ghost + place/break allowed
        bool orbiting = building && build.orbitMode;
        if (testing != lastTesting) { lastTesting = testing; menu.close(); }

        // Orbit Placement: the camera hangs off the cursor block, so mouse look pans
        // around it and the scroll wheel (orbitDistance) zooms in and out.
        if (orbiting)
            cameraPos = glm::dvec3(build.buildCursor) - glm::dvec3(cameraFront) * build.orbitDistance;

        // Keep the camera inside the room's walls.
        if (inBlueprint) {
            const double lim = BLUEPRINT_HALF - 0.5;
            cameraPos = glm::clamp(cameraPos, glm::dvec3(-lim), glm::dvec3(lim));
        }

        // Ghost / edit targeting. Orbit mode acts entirely on the cursor block, so the
        // ray is only cast in free mode. Voxels are rendered centred on integer world
        // coords (cube spans [v-0.5, v+0.5]), but the DDA treats integers as cell
        // corners; shift the ray origin by +0.5 per axis so its cells line up.
        glm::ivec3 hitV(0), prevV(0);
        bool rayHit = false;
        build.ghostValid = false;
        if (!orbiting) {
            // Always know what the crosshair points at: the ghost needs it while building,
            // and E inspects it the rest of the time.
            glm::dvec3 rayOrigin = cameraPos + glm::dvec3(0.5), rayDir(cameraFront);
            rayHit = inBlueprint
                ? raycastVoxel(rayOrigin, rayDir, EDIT_REACH, hitV, prevV, isSolidBP)
                : raycastVoxel(rayOrigin, rayDir, EDIT_REACH, hitV, prevV, isSolid);
        }
        if (building) {
            if (orbiting) {
                build.ghostPos = build.buildCursor;   // orbit: the cursor is the target
                build.ghostValid = true;
            } else if (rayHit) {
                build.ghostPos = prevV;               // free: the face the crosshair points at
                build.ghostValid = true;
            } else if (inBlueprint) {
                // Nothing in reach inside the room: build in mid-air a few blocks ahead.
                build.ghostPos = glm::ivec3(glm::round(
                    cameraPos + glm::dvec3(cameraFront) * BLUEPRINT_AIR_REACH));
                build.ghostValid = true;
            }
        }
        // The ghost spot may already hold a block (orbit mode can steer into one), and in
        // the room the volume cap can refuse it too. Either way it shows red and won't place.
        bool ghostOccupied = build.ghostValid && solidAt(build.ghostPos);
        bool volumeFull = inBlueprint && blueprintVolume >= BLUEPRINT_MAX_VOLUME;
        bool canPlace = build.ghostValid && !ghostOccupied && !volumeFull;

        bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        bool placeBlock = leftDown && !hasLeftDown;
        bool breakBlock = rightDown && !hasRightDown;
        bool menuClick  = anyMenu && placeBlock;     // with an overlay up, a click is for it
        if (building && !anyMenu) {
            if (breakBlock) {
                // Orbit: break the orbited block itself (if one is there); free: the ray hit.
                bool haveTarget   = orbiting ? ghostOccupied : rayHit;
                glm::ivec3 target = orbiting ? build.buildCursor : hitV;
                if (haveTarget) {
                    if (inBlueprint) bpBreak(target);
                    else             breakBlockAt(target);
                }
            } else if (placeBlock && build.ghostValid) {
                if (canPlace) {
                    if (inBlueprint) bpPlace(build.ghostPos, blockSelected, build.blockRotation);
                    else             placeBlockAt(build.ghostPos, blockSelected, build.blockRotation);
                } else if (volumeFull && !ghostOccupied) {
                    char msg[64];
                    std::snprintf(msg, sizeof(msg), "Volume limit reached (%d/%d)",
                                  blueprintVolume, BLUEPRINT_MAX_VOLUME);
                    alerts.show(msg, 1.5f, 6.0f, glm::vec3(1.0f, 0.35f, 0.3f));
                }
            }
        }
        hasLeftDown = leftDown;
        hasRightDown = rightDown;

        // ---- Factory simulation -----------------------------------------------------
        // Targets are rebuilt every frame (placing a block can reallocate `constructs`) and
        // cover the mode being shown; the parked mode's machines don't run. Inside the room
        // the sim only runs in Testing, so a half-built component sits still while designing.
        sim.targets.clear();
        if (inBlueprint) sim.targets.push_back(&blueprint);
        else for (auto& c : constructs) sim.targets.push_back(&c);
        sim.portsActive = inBlueprint;   // ports are the room's test rig; no free items in the world
        bool simRunning = !inBlueprint || testing;
        if (simRunning && !pause.open) sim.update(deltaTime);   // paused: the world stands still
        else if (!simRunning) sim.accumulator = 0.0f;

        // Blocks that show their contents (fluid pipes, tanks) flag their construct when the
        // look changed; re-mesh those now, on the GL thread.
        for (Construct* c : sim.targets)
            if (c->meshDirty) { rebuildConstruct(*c); c->meshDirty = false; }

        // The machine under the crosshair (orbit mode: the cursor block).
        bool haveInspect = orbiting ? ghostOccupied : rayHit;
        glm::ivec3 inspectPos = orbiting ? build.buildCursor : hitV;
        BlockEntity* inspect = haveInspect ? sim.find(inspectPos) : nullptr;

        // E: closes whichever menu is up; otherwise opens the build menu while building, or
        // the block menu on the targeted machine in play / testing (the room's Building mode
        // is for building only). Ignored while paused.
        bool canInteract = !inBlueprint || testing;
        if (build.interact && !pause.open) {
            if (menu.open)             menu.close();
            else if (buildMenu.open)   buildMenu.open = false;
            else if (building)         buildMenu.open = true;
            else if (canInteract && inspect) menu.openAt(inspectPos);
        }
        // Esc: backs out of whatever is up, one level at a time; with nothing up it pauses.
        if (build.closeMenu) {
            if (pause.open)            pause.open = false;
            else if (menu.open)        menu.onEscape();
            else if (buildMenu.open)   buildMenu.open = false;
            else                       pause.open = true;
        }
        BlockEntity* menuEntity = menu.open ? sim.find(menu.pos) : nullptr;
        if (menu.open && !menuEntity) menu.close();      // the block went away

        // Debug item drop (I): into the open menu's block, else the targeted one. Fluid blocks
        // get water, machines the first input their recipes take, everything else an ingot.
        if (build.insertItem && !pause.open) {
            if (BlockEntity* dst = menuEntity ? menuEntity : inspect) {
                ItemId give = ItemId::IronIngot;
                if (dst->def().fluid) give = ItemId::Water;
                else if (dst->behavior() == BlockBehavior::Machine)
                    for (const Recipe& r : recipeTable)
                        if (r.machine == dst->type) { give = r.inputs[0].item; break; }
                uint16_t n = dst->insert(give, 1);
                alerts.show(n ? "+1 " + std::string(itemInfo(give).name) : "Block won't take it",
                            0.8f, 4.0f, n ? glm::vec3(0.5f, 1.0f, 0.6f) : glm::vec3(1.0f, 0.35f, 0.3f));
            }
        }


        // Floating origin: camera stays at (0,0,0); each chunk is offset by its
        // camera-relative origin in the shader, so view holds rotation only.
        glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f),
            cameraFront,
            cameraUp
        );

        glUseProgram(shaderProgram);

        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));


        glClearColor(fogColor.r, fogColor.g, fogColor.b, 1.0f);  // sky matches the fog so the horizon blends
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glUniform1i(glGetUniformLocation(shaderProgram, "texture1"), 0);

        // Global lighting / fog / log-depth (constant across all chunks this frame).
        glUniform1f(logFcoefLoc, logFcoef);
        glUniform3fv(sunDirLoc, 1, glm::value_ptr(sunDir));
        glUniform3fv(sunColorLoc, 1, glm::value_ptr(sunColor));
        glUniform1f(ambientLoc, ambient);
        glUniform3fv(fogColorLoc, 1, glm::value_ptr(fogColor));
        glUniform1f(fogStartLoc, FOG_START);
        glUniform1f(fogEndLoc, FOG_END);

        glBindVertexArray(VAO);

        if (!inBlueprint) {
            // ---- LOD-ring chunk streaming ---------------------------------------
            glm::dvec3 camBlock = glm::dvec3(cameraPos);   // camera position in world blocks

            // Every chunk we want resident this frame. Anything loaded but absent here is
            // unloaded, which cleanly handles both moving out of range and LOD transitions.
            std::unordered_set<ChunkKey, tuple_hash> desired;

            for (int lod = 0; lod <= MAX_LOD; ++lod) {
                const int stride = 1 << lod;
                const int span   = CHUNK_SIZE * stride;                       // world blocks per chunk
                const glm::ivec3 camChunk = glm::ivec3(glm::floor(camBlock / (double)span));

                for (int dx = -LOD_LOAD_RADIUS; dx <= LOD_LOAD_RADIUS; ++dx)
                for (int dz = -LOD_LOAD_RADIUS; dz <= LOD_LOAD_RADIUS; ++dz) {
                    const int cx = camChunk.x + dx;
                    const int cz = camChunk.z + dz;

                    const double x0 = (double)cx * span, x1 = x0 + span;   // chunk footprint in world blocks
                    const double z0 = (double)cz * span, z1 = z0 + span;
                    auto clampd = [](double v, double a, double b) { return v < a ? a : (v > b ? b : v); };

                    // Coverage by 3D-annulus overlap (gap-free). LOD L owns the shell of 3D distance
                    // [innerL, outerL); load this chunk if its actual extent — footprint x surface
                    // height range — overlaps that shell. Testing the chunk's range (not just its
                    // centre) is what closes the holes between rings; 3D distance also keeps terrain
                    // far below a high camera coarse.
                    const double innerL = (lod == 0) ? 0.0
                        : (double)LOD_RING_CHUNKS * (double)(CHUNK_SIZE << (lod - 1));
                    const double outerL = (lod == MAX_LOD) ? 1e30
                        : (double)LOD_RING_CHUNKS * (double)(CHUNK_SIZE << lod);

                    // Cheap horizontal early-out before the expensive surface sampling: 3D distance
                    // is never smaller than the horizontal distance, so if the footprint's nearest
                    // horizontal point already exceeds this LOD's outer radius it can't be in the shell.
                    const double hNear = glm::length(glm::dvec2(
                        clampd(camBlock.x, x0, x1) - camBlock.x, clampd(camBlock.z, z0, z1) - camBlock.z));
                    if (hNear >= outerL) continue;

                    // Sample the surface across the whole chunk footprint (a grid, not just the
                    // centre): a chunk spans 16 varied columns, so the true surface can fall in a
                    // different chunk-Y than the centre predicts. We need its full height range both
                    // to pick the LOD (3D) and to load every chunk-Y the terrain occupies.
                    double sMin = 1e30, sMax = -1e30;
                    for (int gx = 0; gx <= 4; ++gx)
                    for (int gz = 0; gz <= 4; ++gz) {
                        double s = terrainHeightBlocks(heightMapData, heightmapWidth, heightmapHeight,
                                                       (int)(x0 + gx * span / 4.0), (int)(z0 + gz * span / 4.0));
                        sMin = std::min(sMin, s);
                        sMax = std::max(sMax, s);
                    }

                    // nearest / farthest distance from the camera to the chunk's 3D box [x0,x1]x[sMin,sMax]x[z0,z1]
                    glm::dvec3 nearP(clampd(camBlock.x, x0, x1), clampd(camBlock.y, sMin, sMax), clampd(camBlock.z, z0, z1));
                    glm::dvec3 farP(
                        std::abs(camBlock.x - x0) > std::abs(camBlock.x - x1) ? x0 : x1,
                        std::abs(camBlock.y - sMin) > std::abs(camBlock.y - sMax) ? sMin : sMax,
                        std::abs(camBlock.z - z0) > std::abs(camBlock.z - z1) ? z0 : z1);
                    const double nearDist = glm::length(nearP - camBlock);
                    const double farDist  = glm::length(farP - camBlock);
                    if (!(nearDist < outerL && farDist >= innerL)) continue;  // doesn't touch this LOD's shell

                    // Vertical band covers every chunk-Y the terrain occupies in this footprint,
                    // plus margin. If any of it is below sea level, also include the sea-cap chunk.
                    int loY = (int)std::floor(sMin / span) - LOD_VBAND_BELOW;
                    int hiY = (int)std::floor(sMax / span) + LOD_VBAND_ABOVE;
                    if (sMin < SEA_LEVEL)
                        loY = std::min(loY, (int)std::floor((double)SEA_LEVEL / span) - 1);
                    if (hiY - loY > 12) loY = hiY - 12;       // safety clamp on band height

                    for (int cy = loY; cy <= hiY; ++cy) {
                        ChunkKey key = {lod, cx, cy, cz};
                        desired.insert(key);

                        if (loadedChunks.find(key) == loadedChunks.end() &&
                            pendingChunks.find(key) == pendingChunks.end()) {
                            pendingChunks.insert(key);

                            ChunkGenerationTask task;
                            task.lod = lod;
                            task.chunkCoord = glm::vec3(cx, cy, cz);
                            task.priority = nearDist;            // nearest chunks generated first
                            task.heightMapData = heightMapData;  // Read-only, safe to share
                            task.heightmapWidth = heightmapWidth;
                            task.heightmapHeight = heightmapHeight;
                            generationQueue.push(std::move(task));
                        }
                    }
                }
            }

            // Integrate finished chunks (OpenGL calls must run here on the main thread). Capped per
            // frame so a burst from the worker pool can't stall; completedQueue is nearest-first, so
            // the chunks you're looking at upload first and the rest follow over the next frames.
            ChunkGenerationTask completed;
            int uploadsThisFrame = 0;
            while (uploadsThisFrame < MAX_UPLOADS_PER_FRAME && !completedQueue.empty() && completedQueue.pop(completed)) {
                ++uploadsThisFrame;
                ChunkKey key = {completed.lod, (int)completed.chunkCoord.x,
                                (int)completed.chunkCoord.y, (int)completed.chunkCoord.z};
                pendingChunks.erase(key);
                if (!completed.success) continue;

                Chunk chunk;
                chunk.pos = completed.chunkCoord;
                chunk.lod = completed.lod;
                chunk.instanceGroups = std::move(completed.instanceGroups);
                chunk.worldMin = completed.worldMin;
                chunk.worldMax = completed.worldMax;
                chunk.empty = completed.empty;

                // LOD-0 chunks carry a voxel grid; mirror it into blockData so raycasts/edits
                // operate on the same world the mesh shows. Layout matches buildChunkMesh: x-major.
                if (!completed.blocks.empty()) {
                    for (int x = 0; x < CHUNK_SIZE; ++x)
                    for (int y = 0; y < CHUNK_SIZE; ++y)
                    for (int z = 0; z < CHUNK_SIZE; ++z)
                        chunk.blockData[x][y][z] = completed.blocks[(x * CHUNK_SIZE + y) * CHUNK_SIZE + z];
                }

                chunk.instanceVBO.atlasOffsetLoc = atlasOffsetLoc;
                chunk.instanceVBO.tileSizeLoc = tileSizeLoc;
                chunk.instanceVBO.chunkOriginLoc = chunkOriginLoc;
                chunk.instanceVBO.depthBiasLoc = depthBiasLoc;
                chunk.instanceVBO.tileSize = tileSize;

                if (!chunk.empty) {
                    chunk.instanceVBO.upload(chunk.instanceGroups);  // one-time GPU upload
                    chunk.instanceGroups.clear();                    // free the CPU-side copy
                }

                // Sea surface (built on the worker): one-time upload, then drop the CPU copy.
                chunk.waterData = std::move(completed.water);
                if (!chunk.waterData.verts.empty()) {
                    chunk.waterGPU.upload(chunk.waterData);
                    chunk.waterData.verts.clear();
                    chunk.waterData.verts.shrink_to_fit();
                }

                loadedChunks[key] = std::move(chunk);
            }

            // Unload everything not wanted this frame, freeing its GPU buffers.
            for (auto it = loadedChunks.begin(); it != loadedChunks.end();) {
                if (desired.find(it->first) == desired.end()) {
                    if (!it->second.empty) it->second.instanceVBO.deleteBuffers();
                    it->second.waterGPU.destroy();
                    it = loadedChunks.erase(it);
                } else {
                    ++it;
                }
            }

            glm::mat4 viewProjection = projection * view;
            auto frustumPlanes = extractFrustumPlanes(viewProjection);

            waterDraws.clear();
            for (auto& chunkEntry : loadedChunks) {
                Chunk& chunk = chunkEntry.second;

                // Work in camera-relative space so the frustum test stays precise at planet scale.
                glm::vec3 relMin = glm::vec3(glm::dvec3(chunk.worldMin) - glm::dvec3(cameraPos));
                glm::vec3 relMax = glm::vec3(glm::dvec3(chunk.worldMax) - glm::dvec3(cameraPos));

                if (!isAABBInFrustum(frustumPlanes, relMin, relMax)) continue;
                if (chunk.waterGPU.vertexCount > 0)
                    waterDraws.push_back({ &chunk, glm::vec2(relMin.x, relMin.z) });
                if (chunk.empty) continue;

                chunk.instanceVBO.originRelCam = relMin; // chunk origin relative to camera
                // Adjacent LOD rings overlap by design (gap-free); nudge coarser ones back in depth so
                // the finer LOD wins where they overlap. (The shader writes gl_FragDepth for log-depth,
                // which ignores glPolygonOffset, so the bias is applied in the shader instead.)
                chunk.instanceVBO.depthBias = (float)chunk.lod * LOD_DEPTH_BIAS;
                chunkCount ++;
                chunk.draw();
            }

            // Editable constructs share the chunk render path (same VAO/shader/uniforms);
            // they are LOD-0 detail so no depth bias is applied.
            for (auto& c : constructs) {
                if (c.empty()) continue;
                glm::vec3 relMin = glm::vec3(glm::dvec3(c.worldMin) - glm::dvec3(cameraPos));
                glm::vec3 relMax = glm::vec3(glm::dvec3(c.worldMax) - glm::dvec3(cameraPos));
                if (!isAABBInFrustum(frustumPlanes, relMin, relMax)) continue;
                c.instanceVBO.originRelCam = relMin;
                c.instanceVBO.depthBias = 0.0f;
                c.draw();
            }

            // ---- Water pass: the animated sea surface, blended over the opaque world -------
            if (!waterDraws.empty()) {
                // Finer LODs draw first: where LOD rings overlap, the coarser copy then fails
                // the depth test (depth writes stay on) instead of blending the water twice.
                std::sort(waterDraws.begin(), waterDraws.end(),
                          [](const WaterDraw& a, const WaterDraw& b) { return a.chunk->lod < b.chunk->lod; });

                waterRenderer.begin(view, projection, logFcoef, gameTime,
                                    (float)((double)WATER_SURFACE_Y - cameraPos.y),
                                    sunDir, sunColor, ambient, fogColor, FOG_START, FOG_END);
                for (const auto& wd : waterDraws) {
                    // Wave phase must come from ABSOLUTE world position (camera-relative would
                    // make the pattern ride along with the camera). Chunk origins are far too
                    // large for floats, so pass them modulo the wave period — origins are exact
                    // multiples of 16, so the fmod is exact and chunk seams stay seamless.
                    auto modP = [](double v) {
                        double m = std::fmod(v, (double)WATER_WAVE_PERIOD);
                        return (float)(m < 0.0 ? m + (double)WATER_WAVE_PERIOD : m);
                    };
                    glm::vec2 waveOrigin(modP(wd.chunk->worldMin.x), modP(wd.chunk->worldMin.z));
                    waterRenderer.drawChunk(wd.chunk->waterGPU, wd.relXZ, waveOrigin,
                                            (float)wd.chunk->lod * LOD_DEPTH_BIAS);
                }
                waterRenderer.end();
            }
        } else {
            // ---- Blueprint room: the world is parked (chunks stay resident, streaming
            // paused); draw the room's walls, then the component being designed.
            roomRenderer.draw(BLUEPRINT_HALF, glm::vec3(-cameraPos), view, projection,
                              logFcoef, tileSize, glm::ivec2(BLUEPRINT_TILE_X, BLUEPRINT_TILE_Y),
                              sunDir, sunColor, ambient);

            // The room used its own program/VAO; the blueprint's blocks share the chunk
            // pipeline (its uniforms were set on shaderProgram at the top of the frame).
            glUseProgram(shaderProgram);
            glBindVertexArray(VAO);
            if (!blueprint.empty()) {
                blueprint.instanceVBO.originRelCam = glm::vec3(glm::dvec3(blueprint.worldMin) - cameraPos);
                blueprint.instanceVBO.depthBias = 0.0f;
                blueprint.draw();
            }
        }

        // Placement preview: the block ghosted at its target spot in its current
        // rotation — green when placeable, red when it can't be (occupied / room full).
        if (build.ghostValid) {
            glm::vec3 ghostRel = glm::vec3(glm::dvec3(build.ghostPos) - cameraPos);
            glm::vec3 tint = canPlace ? glm::vec3(0.3f, 1.0f, 0.35f)
                                      : glm::vec3(1.0f, 0.25f, 0.2f);
            ghostRenderer.draw(blockSelected, build.blockRotation, ghostRel,
                               view, projection, logFcoef, tileSize, tint, 0.55f);
        }

        char hudLine[160];
        if (inBlueprint)
            std::snprintf(hudLine, sizeof(hudLine), "FPS: %d\nBLUEPRINT [%s] %d/%d\nX: %.1f\nY: %.1f\nZ: %.1f",
                          displayFps, testing ? "TEST" : "BUILD", blueprintVolume, BLUEPRINT_MAX_VOLUME,
                          cameraPos.x, cameraPos.y, cameraPos.z);
        else
            std::snprintf(hudLine, sizeof(hudLine), "FPS: %d\nX: %.1f\nY: %.1f\nZ: %.1f",
                          displayFps, cameraPos.x, cameraPos.y, cameraPos.z);
        std::string hud = hudLine;
        if (building) {
            hud += "\nBlock: ";
            hud += blockTable[blockSelected].name;
            hud += " (rot " + std::to_string(build.blockRotation) + ")";
            if (!anyMenu) hud += "\n[E] blocks";
        }
        if (inspect && canInteract && !building && !anyMenu) {   // prompt for the machine in view
            hud += "\n[E] ";
            hud += blockTable[inspect->type].name;
        }

        if (!pause.open) {   // the pause sheet carries its own readout
            textRenderer.drawText(hud, 11.0f, 11.0f, 2.0f, 0.0f, 0.0f, 0.0f, fbW, fbH);
            textRenderer.drawText(hud, 10.0f, 10.0f, 2.0f, 1.0f, 1.0f, 1.0f, fbW, fbH);
        }

        if (!anyMenu) {
            float cs = 3.0f;
            float cx = fbW * 0.5f - textRenderer.textWidth("+", cs) * 0.5f;
            float cy = fbH * 0.5f - TextRenderer::lineHeight(cs) * 0.5f;
            textRenderer.drawText("+", cx + 1.0f, cy + 1.0f, cs, 0.0f, 0.0f, 0.0f, fbW, fbH);
            textRenderer.drawText("+", cx, cy, cs, 1.0f, 1.0f, 1.0f, fbW, fbH);
        }

        // Overlays (at most one is up): the pause sheet, the build menu, or the block menu.
        // The block menu is drawn from the live entity so belts visibly advance while it is
        // open; clicks and captured keys are applied to that entity here too.
        MenuPointer ptr{ pointerPx, menuClick };
        if (pause.open) {
            int entityTotal = 0;
            for (Construct* c : sim.targets) entityTotal += (int)c->entities.size();
            std::vector<std::string> dbg;
            char line[160];
            auto add = [&](auto... args) { std::snprintf(line, sizeof(line), args...); dbg.emplace_back(line); };
            add("FPS %d  (%.1f ms)", displayFps, deltaTime * 1000.0f);
            add("Pos %.1f  %.1f  %.1f", cameraPos.x, cameraPos.y, cameraPos.z);
            add("Look yaw %.0f  pitch %.0f", yaw, pitch);
            add("Mode: %s", inBlueprint ? (testing ? "Blueprint / Testing" : "Blueprint / Building")
                                        : (build.buildMode ? "World / Build" : "World / Play"));
            add("Chunks loaded %zu  drawn %d  pending %zu", loadedChunks.size(), chunkCount, pendingChunks.size());
            add("Constructs %zu  entities %d", constructs.size(), entityTotal);
            add("Blueprint volume %d / %d", blueprintVolume, BLUEPRINT_MAX_VOLUME);
            add("Selected: %s  rot %d", blockTable[blockSelected].name, build.blockRotation);
            add("Sim: %s  %.0f Hz  ports %s", simRunning ? "running" : "parked",
                FACTORY_TICK_RATE, sim.portsActive ? "active" : "off");
            add("Game time %.1f s", gameTime);
            add("Workers %u", numWorkers);

            PauseAction act = pause.frame(ui, textRenderer, itemTexture, fbW, fbH, ptr, dbg);
            if (act == PauseAction::Resume) pause.open = false;
            else if (act == PauseAction::Quit) glfwSetWindowShouldClose(window, GLFW_TRUE);
        } else if (buildMenu.open) {
            buildMenu.frame(ui, textRenderer, itemTexture, texture, fbW, fbH, ptr,
                            blockSelected, build.blockRotation);
        } else if (menuEntity) {
            MenuKeys keys{ build.letterKey, build.backspaceKey };
            menu.frame(ui, textRenderer, *menuEntity, sim, alerts, itemTexture, fbW, fbH,
                       ptr, keys, (double)currentFrame);
        }

        alerts.draw(textRenderer, fbW, fbH, pause.open ? 0.0f : deltaTime);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    textRenderer.destroy();
    ui.destroy();
    ghostRenderer.destroy();
    roomRenderer.destroy();
    waterRenderer.destroy();
    blueprint.instanceVBO.deleteBuffers();
    glDeleteProgram(shaderProgram);
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteTextures(1, &texture);
    glDeleteTextures(1, &itemTexture);

    generationQueue.stop();
    for (auto& w : workers)
        if (w.joinable()) w.join();

    // Clean up chunks
    for (auto& [key, chunk] : loadedChunks) {
        chunk.instanceVBO.deleteBuffers();
        chunk.waterGPU.destroy();
    }

    // Clean up constructs
    for (auto& c : constructs) {
        c.instanceVBO.deleteBuffers();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}