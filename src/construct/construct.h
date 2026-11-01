#pragma once

#include <map>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/constants.h"
#include "core/hash.h"
#include "render/instance.h"

#include "core/block.h"
#include "sim/entity.h"

// A free-floating, editable voxel object. Unlike a terrain Chunk it is not tied
// to the chunk grid: it has its own world-block origin and a grid that grows to
// fit whatever blocks are edited into it (see setBlock). Edits attach to the
// nearest construct within CONSTRUCT_MERGE_DISTANCE; otherwise a new one is born.
struct Construct {
    InstanceVBO instanceVBO;
    std::vector<std::vector<std::vector<Block>>> blockData;   // layer 2: one Block per cell
    glm::ivec3 origin{0};   // world-block coords of blockData[0][0][0]
    glm::ivec3 size{0};     // grid dimensions in blocks (0 until the first block)
    std::map<InstanceKey, std::vector<InstanceData>> instanceGroups;
    glm::vec3 worldMin, worldMax;

    // Layer 3: dynamic state, only for the cells that are machines (see sim/entity.h).
    // Dense vector so ticking is a linear walk; the index is for neighbour lookup by
    // world position and is patched on swap-and-pop removal.
    std::vector<BlockEntity> entities;
    std::unordered_map<glm::ivec3, uint32_t, IVec3Hash> entityIndex;
    bool meshDirty = false;   // the sim sets this when a block's look changed (fluid colours)

    void updateBounds();
    void genInstances();
    void draw();

    // --- editing / queries (world-block coordinates) ---
    bool contains(glm::ivec3 wp) const;    // is wp inside the grid's extent?
    bool isSolidAt(glm::ivec3 wp) const;   // contains(wp) && that block isn't AIR
    int  distanceTo(glm::ivec3 wp) const;  // Chebyshev distance from wp to the grid box (0 if inside)
    void setBlock(glm::ivec3 wp, Block b); // writes wp, growing/shifting the grid to fit
    void rebuild();                        // regen the mesh and re-upload it to the GPU

    const Block*       blockAt(glm::ivec3 wp) const;   // null outside the grid
    BlockEntity*       entityAt(glm::ivec3 wp);        // null if that cell has no entity
    const BlockEntity* entityAt(glm::ivec3 wp) const;
    void               removeEntity(glm::ivec3 wp);

    bool empty() const { return size.x == 0 || size.y == 0 || size.z == 0; }
};
