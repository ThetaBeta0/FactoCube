#include "render/ui.h"

#include <iostream>

#include <glad/glad.h>

namespace {

// A unit quad stretched to uRect (pixels, top-left origin) and mapped to clip space.
const char* kVertexSrc = R"(#version 330 core
layout (location = 0) in vec2 aPos;   // unit quad corner, (0,0) top-left .. (1,1) bottom-right

uniform vec2 uScreen;   // framebuffer size in pixels
uniform vec4 uRect;     // x, y, w, h in pixels

out vec2 vUV;

void main()
{
    vec2 px  = uRect.xy + aPos * uRect.zw;
    vec2 ndc = vec2(px.x / uScreen.x * 2.0 - 1.0,
                    1.0 - px.y / uScreen.y * 2.0);   // flip Y: pixels are top-down
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aPos;
}
)";

// Solid colour, or colour * one atlas tile. Textures are loaded flipped (row 0 = bottom), so
// the quad's top samples the tile's top by inverting v.
const char* kFragmentSrc = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform vec4      uColor;
uniform int       uUseTex;
uniform sampler2D uTex;
uniform vec2      uAtlasOffset;   // tile coordinates
uniform float     uTileSize;      // 1 / tiles per row

void main()
{
    vec4 c = uColor;
    if (uUseTex == 1)
        c *= texture(uTex, (uAtlasOffset + vec2(vUV.x, 1.0 - vUV.y)) * uTileSize);
    FragColor = c;
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
        std::cerr << "ERROR::UI_SHADER::" << label << "::COMPILE\n" << log << std::endl;
    }
    return s;
}

} // namespace

bool UIRenderer::init(Logger& logger) {
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
        std::cerr << "ERROR::UI_SHADER::PROGRAM::LINK\n" << log << std::endl;
        logger.logError("UI renderer shader failed to link");
        return false;
    }

    uScreenLoc = glGetUniformLocation(program, "uScreen");
    uRectLoc   = glGetUniformLocation(program, "uRect");
    uColorLoc  = glGetUniformLocation(program, "uColor");
    uUseTexLoc = glGetUniformLocation(program, "uUseTex");
    uAtlasLoc  = glGetUniformLocation(program, "uAtlasOffset");
    uTileLoc   = glGetUniformLocation(program, "uTileSize");

    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "uTex"), 0);
    glUniform1f(uTileLoc, 1.0f / 16.0f);
    glUseProgram(0);

    // Two triangles covering the unit square; everything else comes from uniforms.
    const float verts[] = { 0,0,  1,0,  1,1,   0,0,  1,1,  0,1 };
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    logger.logSuccess("UI renderer initialized");
    return true;
}

void UIRenderer::begin(int w, int h) {
    fbW = w; fbH = h;
    savedDepth = glIsEnabled(GL_DEPTH_TEST);
    savedCull  = glIsEnabled(GL_CULL_FACE);
    savedBlend = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST);   // overlay always on top
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void UIRenderer::end() {
    glBindVertexArray(0);
    if (savedDepth) glEnable(GL_DEPTH_TEST);
    if (savedCull)  glEnable(GL_CULL_FACE);
    if (!savedBlend) glDisable(GL_BLEND);
}

void UIRenderer::quad(float x, float y, float w, float h) {
    // Re-bound every call: text draws in between use their own program / VAO.
    glUseProgram(program);
    glBindVertexArray(vao);
    glUniform2f(uScreenLoc, (float)fbW, (float)fbH);
    glUniform4f(uRectLoc, x, y, w, h);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void UIRenderer::rect(float x, float y, float w, float h, glm::vec4 color) {
    if (program == 0) return;
    glUseProgram(program);
    glUniform4f(uColorLoc, color.r, color.g, color.b, color.a);
    glUniform1i(uUseTexLoc, 0);
    quad(x, y, w, h);
}

void UIRenderer::tile(float x, float y, float w, float h, glm::ivec2 t, unsigned int atlasTexture,
                      glm::vec4 tint) {
    if (program == 0) return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTexture);
    glUseProgram(program);
    glUniform4f(uColorLoc, tint.r, tint.g, tint.b, tint.a);
    glUniform1i(uUseTexLoc, 1);
    glUniform2f(uAtlasLoc, (float)t.x, (float)t.y);
    quad(x, y, w, h);
}

void UIRenderer::destroy() {
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (program) glDeleteProgram(program);
    program = vao = vbo = 0;
}
