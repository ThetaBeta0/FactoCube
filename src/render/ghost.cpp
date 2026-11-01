#include "render/ghost.h"

#include <iostream>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/block.h"
#include "core/geometry.h"

namespace {

const char* kVertexSrc = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

uniform mat4  view;
uniform mat4  projection;
uniform mat4  model;        // block rotation (+ slight shrink against z-fighting)
uniform vec3  blockOrigin;  // voxel centre relative to the camera
uniform float logFcoef;

out vec2  vUV;
out float vFlogz;

void main()
{
    vec3 worldRel = (model * vec4(aPos, 1.0)).xyz + blockOrigin;
    vec4 clip = projection * view * vec4(worldRel, 1.0);
    vFlogz = 1.0 + clip.w;
    clip.z = (log2(max(1e-6, vFlogz)) * logFcoef - 1.0) * clip.w;
    gl_Position = clip;
    vUV = aTexCoord;
}
)";

const char* kFragmentSrc = R"(#version 330 core
in vec2  vUV;
in float vFlogz;

out vec4 FragColor;

uniform sampler2D texture1;
uniform vec2  atlasOffset;
uniform float tileSize;
uniform vec3  tint;
uniform float alpha;
uniform float logFcoef;

void main()
{
    vec4 tex = texture(texture1, (atlasOffset + vUV) * tileSize);
    FragColor = vec4(mix(tex.rgb, tint, 0.55), tex.a * alpha);
    gl_FragDepth = log2(vFlogz) * (logFcoef * 0.5);
}
)";

unsigned int compile(GLenum type, const char* src, const char* label) {
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::cerr << "ERROR::GHOST_SHADER::" << label << "::COMPILE\n" << log << std::endl;
    }
    return s;
}

} // namespace

bool GhostRenderer::init(Logger& logger) {
    unsigned int vs = compile(GL_VERTEX_SHADER, kVertexSrc, "VERTEX");
    unsigned int fs = compile(GL_FRAGMENT_SHADER, kFragmentSrc, "FRAGMENT");

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    int ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::cerr << "ERROR::GHOST_SHADER::PROGRAM::LINK\n" << log << std::endl;
        logger.logError("Ghost renderer shader failed to link");
        return false;
    }

    uViewLoc   = glGetUniformLocation(program, "view");
    uProjLoc   = glGetUniformLocation(program, "projection");
    uModelLoc  = glGetUniformLocation(program, "model");
    uOriginLoc = glGetUniformLocation(program, "blockOrigin");
    uLogFLoc   = glGetUniformLocation(program, "logFcoef");
    uAtlasLoc  = glGetUniformLocation(program, "atlasOffset");
    uTileLoc   = glGetUniformLocation(program, "tileSize");
    uTintLoc   = glGetUniformLocation(program, "tint");
    uAlphaLoc  = glGetUniformLocation(program, "alpha");

    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "texture1"), 0);
    glUseProgram(0);

    // The same 24-vertex cube the scene uses, in a private VAO so the ghost never
    // disturbs the instanced attribute state of the shared chunk VAO.
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

    logger.logSuccess("Ghost renderer initialized");
    return true;
}

void GhostRenderer::draw(int type, int rotation, const glm::vec3& posRelCam,
                         const glm::mat4& view, const glm::mat4& projection,
                         float logFcoef, float tileSize,
                         const glm::vec3& tint, float alpha) {
    if (program == 0) return;

    GLboolean blendOn = glIsEnabled(GL_BLEND);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);   // translucent: test against the scene but don't occlude it

    glUseProgram(program);

    // Shrunk a hair so faces flush with existing geometry don't z-fight.
    glm::mat4 model = blockRotations[rotation % 24]
                    * glm::scale(glm::mat4(1.0f), glm::vec3(0.995f));

    glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, glm::value_ptr(model));
    glUniform3fv(uOriginLoc, 1, glm::value_ptr(posRelCam));
    glUniform1f(uLogFLoc, logFcoef);
    glUniform1f(uTileLoc, tileSize);
    glUniform3fv(uTintLoc, 1, glm::value_ptr(tint));
    glUniform1f(uAlphaLoc, alpha);

    glBindVertexArray(vao);
    for (int f = 0; f < 6; ++f) {
        glm::ivec2 tile = blockTable[type].tiles[f];
        glUniform2f(uAtlasLoc, (float)tile.x, (float)tile.y);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT,
                       (void*)(f * faceIndexOffset * sizeof(unsigned int)));
    }
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    if (!blendOn) glDisable(GL_BLEND);
}

void GhostRenderer::destroy() {
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (program) glDeleteProgram(program);
    program = vao = vbo = ebo = 0;
}
