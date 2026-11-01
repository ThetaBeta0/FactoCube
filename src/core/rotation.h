#pragma once

#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/ext/matrix_int3x3.hpp>

#include "core/geometry.h"

// Integer view of the 24 block orientations. The indices are the SAME as blockRotations
// (geometry.h) -- each entry here is derived from that float matrix -- so game logic and
// the renderer can never disagree about what a Block::rotation value means.
//
// Convention (from blockRotations): index = facing * 4 + spin, where `facing` is the world
// direction the block's LOCAL +Y (Top) ends up pointing. Machines are therefore authored
// with local +Y as their forward / flow axis (Bottom = in, Top = out) and rotation carries
// that into the world.
constexpr int ROTATION_COUNT = 24;

struct RotationSet {
    glm::imat3x3 mat[ROTATION_COUNT];             // local -> world, exact integer entries
    uint8_t faceMap[ROTATION_COUNT][FACE_COUNT];    // local face -> world face it points at
    uint8_t faceMapInv[ROTATION_COUNT][FACE_COUNT]; // world face -> local face pointing there
};

inline RotationSet buildRotationSet() {
    RotationSet rs{};
    for (int r = 0; r < ROTATION_COUNT; ++r) {
        glm::imat3x3 m(0);
        for (int c = 0; c < 3; ++c)
            for (int row = 0; row < 3; ++row)
                m[c][row] = (int)std::lround(blockRotations[r][c][row]);
        rs.mat[r] = m;
        for (int f = 0; f < FACE_COUNT; ++f) {
            int g = faceFromNormal(m * faceOffsets[f]);   // a cube rotation maps axes to axes
            rs.faceMap[r][f] = (uint8_t)g;
            rs.faceMapInv[r][g] = (uint8_t)f;
        }
    }
    return rs;
}

// blockRotations is defined before this in every translation unit (rotation.h includes
// geometry.h), which is what orders the two inline dynamic initialisations.
inline const RotationSet kRot = buildRotationSet();

inline bool isValidRotation(int r) { return r >= 0 && r < ROTATION_COUNT; }

// World face that a block's local face shows up on, and the inverse.
inline Face localToWorldFace(uint8_t rotation, Face local) {
    return static_cast<Face>(kRot.faceMap[rotation][local]);
}
inline Face worldToLocalFace(uint8_t rotation, Face world) {
    return static_cast<Face>(kRot.faceMapInv[rotation][world]);
}

inline glm::ivec3 rotateLocal(uint8_t rotation, glm::ivec3 v) { return kRot.mat[rotation] * v; }

// Unit offset of a block's flow axis (local +Y) in the world.
inline glm::ivec3 blockForward(uint8_t rotation) {
    return faceOffsets[kRot.faceMap[rotation][Top]];
}
