#include "render/room.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include "core/geometry.h"
#include "render/shader.h"

bool RoomRenderer::init(Logger& logger) {
    program = createShaderProgram("shaders/room_vertex.glsl", "shaders/room_fragment.glsl", logger);
    if (program == 0) return false;

    uViewLoc     = glGetUniformLocation(program, "view");
    uProjLoc     = glGetUniformLocation(program, "projection");
    uCentreLoc   = glGetUniformLocation(program, "centreRelCam");
    uSizeLoc     = glGetUniformLocation(program, "size");
    uLogFLoc     = glGetUniformLocation(program, "logFcoef");
    uAtlasLoc    = glGetUniformLocation(program, "atlasOffset");
    uTileLoc     = glGetUniformLocation(program, "tileSize");
    uSunDirLoc   = glGetUniformLocation(program, "sunDir");
    uSunColorLoc = glGetUniformLocation(program, "sunColor");
    uAmbientLoc  = glGetUniformLocation(program, "ambient");

    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "texture1"), 0);
    glUseProgram(0);

    // The scene's unit cube in a private VAO; the shader scales it up to room size.
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Faces), Faces.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Face_Indices), Face_Indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    logger.logSuccess("Room renderer initialized");
    return true;
}

void RoomRenderer::draw(int half, const glm::vec3& centreRelCam,
                        const glm::mat4& view, const glm::mat4& projection,
                        float logFcoef, float tileSize, glm::ivec2 tile,
                        const glm::vec3& sunDir, const glm::vec3& sunColor, float ambient) {
    if (program == 0) return;

    glUseProgram(program);
    glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(uCentreLoc, 1, glm::value_ptr(centreRelCam));
    glUniform1f(uSizeLoc, (float)(2 * half + 1));
    glUniform1f(uLogFLoc, logFcoef);
    glUniform2f(uAtlasLoc, (float)tile.x, (float)tile.y);
    glUniform1f(uTileLoc, tileSize);
    glUniform3fv(uSunDirLoc, 1, glm::value_ptr(sunDir));
    glUniform3fv(uSunColorLoc, 1, glm::value_ptr(sunColor));
    glUniform1f(uAmbientLoc, ambient);

    // The cube's faces wind outward; from inside they are back faces, so cull the
    // fronts for this one draw to keep the inward-facing walls.
    glCullFace(GL_FRONT);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)Face_Indices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glCullFace(GL_BACK);
}

void RoomRenderer::destroy() {
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (program) glDeleteProgram(program);
    program = vao = vbo = ebo = 0;
}
