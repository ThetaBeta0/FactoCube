#pragma once

#include <map>
#include <vector>

#include <glm/glm.hpp>

#include "core/block.h"
#include "core/constants.h"
#include "render/instance.h"
#include "render/water.h"

struct Chunk {
    InstanceVBO instanceVBO;
    Block blockData[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE] = {};   // layer 2: 2 bytes per cell
    std::map<InstanceKey, std::vector<InstanceData>> instanceGroups;
    bool isInitialized = false;
    glm::vec3 pos;
    int lod = 0;
    glm::vec3 worldMin, worldMax;
    bool empty = true;

    // Sea surface for this chunk (only non-empty in the chunk whose Y-range contains
    // SEA_LEVEL). Built on the worker thread, uploaded and drawn by the water pass.
    WaterMeshData waterData;
    WaterMeshGPU waterGPU;

    void updateBounds();

    void genInstances();
    void draw();
    void updateChunk(const std::vector<std::array<int, 4>>& edits);
};

// outBlocks (optional): if non-null, receives the solid-voxel grid (CHUNK_SIZE^3, flattened
// x-major then y then z) so the chunk's blockData can mirror the visible mesh for collision/edits.
// Only meaningful at LOD 0 (1 voxel == 1 world block); pass null for coarse LODs.
// outWater (optional): if non-null and this chunk contains the sea surface, receives the
// water mesh — corner-node triangles over every column whose rendered top is underwater.
void buildChunkMesh(int lod, glm::ivec3 chunkCoord, const unsigned char* hm, int hmW, int hmH,
                    std::map<InstanceKey, std::vector<InstanceData>>& out,
                    glm::vec3& worldMin, glm::vec3& worldMax, bool& empty,
                    std::vector<Block>* outBlocks = nullptr,
                    WaterMeshData* outWater = nullptr);
