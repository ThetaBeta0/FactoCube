#include "render/water.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include "core/constants.h"
#include "render/shader.h"

void WaterMeshGPU::upload(const WaterMeshData& data) {
    vertexCount = (int)data.verts.size();
    if (vertexCount == 0) return;
    if (vbo == 0) glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, data.verts.size() * sizeof(WaterVertex),
                 data.verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void WaterMeshGPU::destroy() {
    if (vbo) glDeleteBuffers(1, &vbo);
    vbo = 0;
    vertexCount = 0;
}

bool WaterRenderer::init(Logger& logger) {
    program = createShaderProgram("shaders/water_vertex.glsl", "shaders/water_fragment.glsl", logger);
    if (program == 0) {
        logger.logError("Water shader failed to build");
        return false;
    }

    uViewLoc       = glGetUniformLocation(program, "view");
    uProjLoc       = glGetUniformLocation(program, "projection");
    uLogFLoc       = glGetUniformLocation(program, "logFcoef");
    uTimeLoc       = glGetUniformLocation(program, "time");
    uSeaYLoc       = glGetUniformLocation(program, "seaLevelRelCam");
    uOriginLoc     = glGetUniformLocation(program, "chunkOriginXZ");
    uWaveOriginLoc = glGetUniformLocation(program, "waveOrigin");
    uDepthBiasLoc  = glGetUniformLocation(program, "depthBias");
    uSunDirLoc     = glGetUniformLocation(program, "sunDir");
    uSunColorLoc   = glGetUniformLocation(program, "sunColor");
    uAmbientLoc    = glGetUniformLocation(program, "ambient");
    uFogColorLoc   = glGetUniformLocation(program, "fogColor");
    uFogStartLoc   = glGetUniformLocation(program, "fogStart");
    uFogEndLoc     = glGetUniformLocation(program, "fogEnd");
    uAlphaLoc      = glGetUniformLocation(program, "waterAlpha");
    uTileSizeLoc   = glGetUniformLocation(program, "tileSize");
    uTileBaseLoc   = glGetUniformLocation(program, "tileBase");
    uAmpLoc        = glGetUniformLocation(program, "waveAmplitude");
    uFadeStartLoc  = glGetUniformLocation(program, "waveFadeStart");
    uFadeEndLoc    = glGetUniformLocation(program, "waveFadeEnd");
    uPeriodLoc     = glGetUniformLocation(program, "wavePeriod");
    uSpeedLoc      = glGetUniformLocation(program, "waveSpeed");

    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "texture1"), 0);
    // Tuning constants are uploaded once; they only change on recompile (constants.h).
    glUniform1f(uAlphaLoc, 1.0f - WATER_TRANSPARENCY);
    glUniform1f(uTileSizeLoc, 1.0f / 16.0f);
    glUniform2f(uTileBaseLoc, (float)WATER_TILE_BASE_X, (float)WATER_TILE_BASE_Y);
    glUniform1f(uAmpLoc, WATER_WAVE_AMPLITUDE);
    glUniform1f(uFadeStartLoc, WATER_WAVE_FADE_START);
    glUniform1f(uFadeEndLoc, WATER_WAVE_FADE_END);
    glUniform1f(uPeriodLoc, WATER_WAVE_PERIOD);
    glUniform1f(uSpeedLoc, WATER_WAVE_SPEED);
    glUseProgram(0);

    // Private VAO so the water pass never disturbs the shared chunk VAO's attribute state.
    glGenVertexArrays(1, &vao);

    logger.logSuccess("Water renderer initialized");
    return true;
}

void WaterRenderer::begin(const glm::mat4& view, const glm::mat4& projection, float logFcoef,
                          float time, float seaLevelRelCamY,
                          const glm::vec3& sunDir, const glm::vec3& sunColor, float ambient,
                          const glm::vec3& fogColor, float fogStart, float fogEnd) {
    glUseProgram(program);
    glBindVertexArray(vao);

    // Semi-transparent surface drawn after all opaque geometry. Depth WRITES stay on:
    // where LOD rings overlap, the finer ring (drawn first, nudged nearer by depthBias)
    // fills the depth buffer and the coarser copy is rejected instead of blending twice.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);   // the surface should also be visible from underwater

    glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(uLogFLoc, logFcoef);
    glUniform1f(uTimeLoc, time);
    glUniform1f(uSeaYLoc, seaLevelRelCamY);
    glUniform3fv(uSunDirLoc, 1, glm::value_ptr(sunDir));
    glUniform3fv(uSunColorLoc, 1, glm::value_ptr(sunColor));
    glUniform1f(uAmbientLoc, ambient);
    glUniform3fv(uFogColorLoc, 1, glm::value_ptr(fogColor));
    glUniform1f(uFogStartLoc, fogStart);
    glUniform1f(uFogEndLoc, fogEnd);
}

void WaterRenderer::drawChunk(const WaterMeshGPU& mesh, const glm::vec2& originRelCamXZ,
                              const glm::vec2& waveOrigin, float depthBias) {
    if (mesh.vbo == 0 || mesh.vertexCount == 0) return;

    glUniform2fv(uOriginLoc, 1, glm::value_ptr(originRelCamXZ));
    glUniform2fv(uWaveOriginLoc, 1, glm::value_ptr(waveOrigin));
    glUniform1f(uDepthBiasLoc, depthBias);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WaterVertex), (void*)0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
}

void WaterRenderer::end() {
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void WaterRenderer::destroy() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (program) glDeleteProgram(program);
    vao = 0;
    program = 0;
}
