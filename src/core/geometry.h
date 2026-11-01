#pragma once

#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

const unsigned int faceIndexOffset = 6;

enum Face {
    Front,
    Back,
    Left,
    Right,
    Top,
    Bottom
};

static constexpr std::array<unsigned int, 36> Face_Indices = {
    0,  1,  2,  2,  3,  0,
    4,  5,  6,  6,  7,  4,
    8,  9, 10, 10, 11,  8,
    12, 13, 14, 14, 15, 12,
    16, 17, 18, 18, 19, 16,
    20, 21, 22, 22, 23, 20
};

static constexpr std::array<float, 120> Faces = {
    // Front
    -0.5f, -0.5f,  0.5f, 0.0f, 0.0f,
    0.5f, -0.5f,  0.5f, 1.0f, 0.0f,
    0.5f,  0.5f,  0.5f, 1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f, 0.0f, 1.0f,

    // Back
    0.5f, -0.5f, -0.5f, 0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f, 1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f, 1.0f, 1.0f,
    0.5f,  0.5f, -0.5f, 0.0f, 1.0f,

    // Left
    -0.5f, -0.5f, -0.5f, 0.0f, 0.0f,
    -0.5f, -0.5f,  0.5f, 1.0f, 0.0f,
    -0.5f,  0.5f,  0.5f, 1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f, 0.0f, 1.0f,

    // Right
    0.5f, -0.5f,  0.5f, 0.0f, 0.0f,
    0.5f, -0.5f, -0.5f, 1.0f, 0.0f,
    0.5f,  0.5f, -0.5f, 1.0f, 1.0f,
    0.5f,  0.5f,  0.5f, 0.0f, 1.0f,

    // Top
    -0.5f,  0.5f,  0.5f, 0.0f, 0.0f,
    0.5f,  0.5f,  0.5f, 1.0f, 0.0f,
    0.5f,  0.5f, -0.5f, 1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f, 0.0f, 1.0f,

    // Bottom
    -0.5f, -0.5f, -0.5f, 0.0f, 0.0f,
    0.5f, -0.5f, -0.5f, 1.0f, 0.0f,
    0.5f, -0.5f,  0.5f, 1.0f, 1.0f,
    -0.5f, -0.5f,  0.5f, 0.0f, 1.0f
};

constexpr int FACE_COUNT = 6;

static const glm::ivec3 faceOffsets[6] = {
    { 0,  0,  1}, // Front
    { 0,  0, -1}, // Back
    {-1,  0,  0}, // Left
    { 1,  0,  0}, // Right
    { 0,  1,  0}, // Top
    { 0, -1,  0}  // Bottom
};

inline Face oppositeFace(Face f) {
    static const Face opp[6] = { Back, Front, Right, Left, Bottom, Top };
    return opp[f];
}

// The face whose outward normal is `n` (a unit axis offset), or -1 if `n` isn't one.
inline int faceFromNormal(glm::ivec3 n) {
    for (int f = 0; f < FACE_COUNT; ++f)
        if (faceOffsets[f] == n) return f;
    return -1;
}

inline auto isInBounds = [](int x, int y, int z, int worldSize) -> bool {
    return x >= 0 && x < worldSize &&
           y >= 0 && y < worldSize &&
           z >= 0 && z < worldSize;
};

inline auto isInSize = [](int x, int y, int z, glm::ivec3 size) -> bool {
    return x >= 0 && x < size.x &&
           y >= 0 && y < size.y &&
           z >= 0 && z < size.z;
};

// The 24 orientations of a cube (its proper rotation group), laid out in a
// logical, decodable order:  index = facing * 4 + spin.
//   facing (0-5) = which world direction the block's local +Y (up) points to,
//                  ordered  +Y, -Y, +X, -X, +Z, -Z
//   spin   (0-3) = number of 90-degree turns about that facing axis.
// Index 0 is the identity (unrotated). Each entry is a rotation about the cube
// centre, so it can be multiplied straight into a face's model matrix to rotate
// and display the block. All 24 are proper rotations (det +1) so triangle
// winding / backface culling is preserved.
inline const std::array<glm::mat4, 24> blockRotations = [] {
    constexpr float HALF_PI = 1.57079632679489661923f;

    const glm::vec3 axisX(1.0f, 0.0f, 0.0f);
    const glm::vec3 axisY(0.0f, 1.0f, 0.0f);
    const glm::vec3 axisZ(0.0f, 0.0f, 1.0f);

    // Base rotation bringing local +Y onto each of the 6 world directions.
    const glm::mat4 base[6] = {
        glm::mat4(1.0f),                                       // +Y (up)
        glm::rotate(glm::mat4(1.0f), 2.0f * HALF_PI, axisX),   // -Y (down)
        glm::rotate(glm::mat4(1.0f),      -HALF_PI, axisZ),    // +X
        glm::rotate(glm::mat4(1.0f),       HALF_PI, axisZ),    // -X
        glm::rotate(glm::mat4(1.0f),       HALF_PI, axisX),    // +Z
        glm::rotate(glm::mat4(1.0f),      -HALF_PI, axisX),    // -Z
    };
    // World axis the up-vector now lies along; the spin turns about it.
    const glm::vec3 facingAxis[6] = { axisY, -axisY, axisX, -axisX, axisZ, -axisZ };

    std::array<glm::mat4, 24> rots{};
    for (int f = 0; f < 6; ++f)
        for (int s = 0; s < 4; ++s)
            rots[f * 4 + s] =
                glm::rotate(glm::mat4(1.0f), static_cast<float>(s) * HALF_PI, facingAxis[f]) * base[f];
    return rots;
}();