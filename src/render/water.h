#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "logger.h"

// ---------------------------------------------------------------------------------------
// Dynamic sea surface, rendered as its own pass on top of the opaque terrain.
//
// How it works, end to end:
//  * While a chunk is meshed on a worker thread, buildChunkMesh also emits a WaterMeshData:
//    a flat grid of triangles whose nodes sit on BLOCK CORNERS, covering exactly the
//    columns whose rendered terrain top is below SEA_LEVEL. The mesh therefore hugs the
//    shoreline of the terrain it was generated with, at that chunk's LOD (coarse chunks
//    get coarse water cells, so distant water costs almost nothing).
//  * Each node carries a "shore" factor: 0 when the node touches a land column, 1 in open
//    water. The vertex shader multiplies the wave height by it, pinning the water's edge
//    to the terrain while the open sea bobs up and down.
//  * All animation happens in the vertex shader from a time uniform — the buffers are
//    uploaded once and never touched again, so per-frame CPU cost is one draw per chunk.
//  * Transparency, wave shape/speed and the atlas tiles are tuned in core/constants.h
//    (WATER_*); the wave functions themselves live in shaders/water_vertex.glsl.
// ---------------------------------------------------------------------------------------

struct WaterVertex {
    float x, z;    // chunk-local position, in blocks (y comes from WATER_SURFACE_Y + wave)
    float shore;   // 0 = node touches land (pinned), 1 = open water (full wave height)
};

// CPU-side mesh, produced on a worker thread. Plain triangles (no index buffer): shared
// corners are duplicated, but the wave height is a pure function of world position, so
// duplicates always move identically and the surface stays crack-free.
struct WaterMeshData {
    std::vector<WaterVertex> verts;
};

// Per-chunk GPU buffer. Upload/destroy must run on the main (GL) thread.
struct WaterMeshGPU {
    unsigned int vbo = 0;
    int vertexCount = 0;

    void upload(const WaterMeshData& data);
    void destroy();
};

// Owns the water shader + VAO and the per-frame render state. Usage per frame:
//   begin(...) once, drawChunk(...) for each visible water chunk (fine LODs first, so
//   overlapping coarse rings lose the depth test instead of double-blending), end().
class WaterRenderer {
public:
    bool init(Logger& logger);
    void destroy();

    // Sets the water GL state (blend on, culling off so the surface is visible from
    // below) and every uniform that is constant across the frame.
    void begin(const glm::mat4& view, const glm::mat4& projection, float logFcoef,
               float time, float seaLevelRelCamY,
               const glm::vec3& sunDir, const glm::vec3& sunColor, float ambient,
               const glm::vec3& fogColor, float fogStart, float fogEnd);

    // originRelCamXZ: chunk world origin minus camera (floating origin), XZ only.
    // waveOrigin:     chunk world origin XZ modulo WATER_WAVE_PERIOD — gives the shader
    //                 seamless absolute-world wave phase without planet-scale floats.
    // depthBias:      same per-LOD nudge the terrain uses, so finer rings win overlaps.
    void drawChunk(const WaterMeshGPU& mesh, const glm::vec2& originRelCamXZ,
                   const glm::vec2& waveOrigin, float depthBias);

    void end();  // restores the terrain pass GL state

private:
    unsigned int program = 0;
    unsigned int vao = 0;

    int uViewLoc = -1, uProjLoc = -1, uLogFLoc = -1, uTimeLoc = -1;
    int uSeaYLoc = -1, uOriginLoc = -1, uWaveOriginLoc = -1, uDepthBiasLoc = -1;
    int uSunDirLoc = -1, uSunColorLoc = -1, uAmbientLoc = -1;
    int uFogColorLoc = -1, uFogStartLoc = -1, uFogEndLoc = -1;
    int uAlphaLoc = -1, uTileSizeLoc = -1, uTileBaseLoc = -1;
    int uAmpLoc = -1, uFadeStartLoc = -1, uFadeEndLoc = -1, uPeriodLoc = -1, uSpeedLoc = -1;
};
