#include "render/text.h"

#include <iostream>
#include <vector>

#include <glad/glad.h>

// stb_easy_font is a header of static functions (no implementation guard); include it in
// this one translation unit only. It writes interleaved vertices: float x,y,z then 4 bytes
// of RGBA colour (16-byte stride), 4 vertices per quad, returning the quad count.
#include <stb-master/stb_easy_font.h>

namespace {

// Maps pixel coordinates (top-left origin, y down) to clip space; reads stb_easy_font's
// per-vertex colour. uScale grows the font around the text origin; uOrigin places it.
const char* kVertexSrc = R"(#version 330 core
layout (location = 0) in vec2 aPos;     // pixel position relative to the text origin
layout (location = 1) in vec4 aColor;   // per-vertex RGBA (from stb_easy_font)

uniform vec2  uScreen;  // framebuffer size in pixels
uniform vec2  uOrigin;  // pixel offset of the text block's top-left corner
uniform float uScale;   // font scale multiplier

out vec4 vColor;

void main()
{
    vec2 px  = aPos * uScale + uOrigin;
    vec2 ndc = vec2(px.x / uScreen.x * 2.0 - 1.0,
                    1.0 - px.y / uScreen.y * 2.0);   // flip Y: pixels are top-down
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = aColor;
}
)";

const char* kFragmentSrc = R"(#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
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
        std::cerr << "ERROR::TEXT_SHADER::" << label << "::COMPILE\n" << log << std::endl;
    }
    return s;
}

} // namespace

bool TextRenderer::init(Logger& logger) {
    unsigned int vs = compile(GL_VERTEX_SHADER, kVertexSrc, "VERTEX");
    unsigned int fs = compile(GL_FRAGMENT_SHADER, kFragmentSrc, "FRAGMENT");

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    int ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::cerr << "ERROR::TEXT_SHADER::PROGRAM::LINK\n" << log << std::endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        logger.logError("Text renderer shader failed to link");
        return false;
    }

    uScreenLoc = glGetUniformLocation(program, "uScreen");
    uOriginLoc = glGetUniformLocation(program, "uOrigin");
    uScaleLoc  = glGetUniformLocation(program, "uScale");

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    // stb_easy_font vertex: float x,y,z (offset 0) + 4 unsigned bytes RGBA (offset 12), stride 16.
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 16, (void*)12);
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    logger.logSuccess("Text renderer initialized");
    return true;
}

void TextRenderer::drawText(const std::string& text, float x, float y, float scale,
                            float r, float g, float b, int screenW, int screenH, float a) {
    if (program == 0 || text.empty()) return;

    // Generate the glyph quads at the origin; the shader applies scale + the (x,y) offset.
    static std::vector<char> verts(0);
    if (verts.size() < 99999) verts.resize(99999);
    unsigned char col[4] = {
        (unsigned char)(r * 255.0f), (unsigned char)(g * 255.0f),
        (unsigned char)(b * 255.0f), (unsigned char)(a * 255.0f) };
    int numQuads = stb_easy_font_print(
        0.0f, 0.0f, const_cast<char*>(text.c_str()), col, verts.data(), (int)verts.size());
    if (numQuads <= 0) return;

    // Core profile has no GL_QUADS, so expand each quad (4 verts) into two triangles.
    static std::vector<unsigned int> indices;
    indices.clear();
    indices.reserve(numQuads * 6);
    for (int q = 0; q < numQuads; ++q) {
        unsigned int base = q * 4;
        indices.insert(indices.end(),
            {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    // Save the bits of GL state we change so the 3D scene render is untouched.
    GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullOn  = glIsEnabled(GL_CULL_FACE);
    GLboolean blendOn = glIsEnabled(GL_BLEND);

    glDisable(GL_DEPTH_TEST);              // overlay always on top
    glDisable(GL_CULL_FACE);               // stb quad winding isn't guaranteed CCW
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(program);
    glUniform2f(uScreenLoc, (float)screenW, (float)screenH);
    glUniform2f(uOriginLoc, x, y);
    glUniform1f(uScaleLoc, scale);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, numQuads * 4 * 16, verts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
                 indices.data(), GL_DYNAMIC_DRAW);

    glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, 0);

    glBindVertexArray(0);

    // Restore the scene's render state.
    if (depthOn) glEnable(GL_DEPTH_TEST);
    if (!cullOn) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
    if (!blendOn) glDisable(GL_BLEND);
}

float TextRenderer::textWidth(const std::string& text, float scale) const {
    return stb_easy_font_width(const_cast<char*>(text.c_str())) * scale;
}

float TextRenderer::lineHeight(float scale) {
    return 7.0f * scale;
}

void TextRenderer::destroy() {
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (program) glDeleteProgram(program);
    program = vao = vbo = ebo = 0;
}
