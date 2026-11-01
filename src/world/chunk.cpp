#include "world/chunk.h"

#include <array>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "core/block.h"
#include "core/geometry.h"
#include "world/biome.h"
#include "world/terrain.h"

void Chunk::updateBounds() {
    // Chunk position in world coordinates (blocks)
    float chunkWorldX = pos.x * CHUNK_SIZE;
    float chunkWorldY = pos.y * CHUNK_SIZE;
    float chunkWorldZ = pos.z * CHUNK_SIZE;
    
    worldMin = glm::vec3(chunkWorldX, chunkWorldY, chunkWorldZ);
    worldMax = glm::vec3(chunkWorldX + CHUNK_SIZE, 
                        chunkWorldY + CHUNK_SIZE, 
                        chunkWorldZ + CHUNK_SIZE);
}

void Chunk::genInstances() {
    bool emptyGen = true;
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                if (blockData[x][y][z].type == AIR) continue;
                emptyGen = false;
                for (int f = 0; f < 6; ++f) {
                    glm::ivec3 neighbor = glm::ivec3(x, y, z) + faceOffsets[f];
                    if (isInBounds(neighbor.x, neighbor.y, neighbor.z, CHUNK_SIZE) && blockData[neighbor.x][neighbor.y][neighbor.z].type != AIR) continue;
                    glm::ivec2 tile = blockTable[blockData[x][y][z].type].tiles[f];
                    glm::vec2 atlasOffset = glm::vec2(tile.x, tile.y);
                    Face face = static_cast<Face>(f);
                    InstanceKey key = {face, atlasOffset};
                    InstanceData data;
                    
                    // Chunk-local position only; the chunk's world origin is applied
                    // (camera-relative) in the shader for floating-origin precision.
                    data.model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
                    data.atlasOffset = atlasOffset;

                    instanceGroups[key].push_back(data);
                }
            }
        }
    }
    empty = emptyGen;
    isInitialized = true;
}

void Chunk::draw() {
    instanceVBO.draw();
}

void Chunk::updateChunk(const std::vector<std::array<int, 4>>& edits) {
    for (auto& edit : edits) {
        blockData[edit[0]][edit[1]][edit[2]] = makeBlock(edit[3], 0);
    }
    instanceGroups.clear();
    genInstances();
    instanceVBO.upload(instanceGroups);  // push the regenerated mesh to the GPU
    instanceGroups.clear();              // free the CPU-side copy (mirrors the initial load)
}

void buildChunkMesh(int lod, glm::ivec3 chunkCoord,
                    const unsigned char* hm, int hmW, int hmH,
                    std::map<InstanceKey, std::vector<InstanceData>>& out,
                    glm::vec3& worldMin, glm::vec3& worldMax, bool& empty,
                    std::vector<Block>* outBlocks, WaterMeshData* outWater) {
    const int stride = 1 << lod;
    const int span   = CHUNK_SIZE * stride;
    const glm::ivec3 origin = chunkCoord * span;          // world-block corner of the chunk

    // Does this chunk's vertical range contain the rendered sea surface? (water cap lives here)
    const int  seaCapY     = (int)std::floor(WATER_SURFACE_Y);
    const bool chunkHasSea = (origin.y <= seaCapY && seaCapY < origin.y + span);

    int  voxels[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];
    bool underwater[CHUNK_SIZE][CHUNK_SIZE] = {};
    int  topSolidY[CHUNK_SIZE][CHUNK_SIZE];                // highest solid voxel per column, -1 if none
    empty = true;
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            int wx = origin.x + x * stride;
            int wz = origin.z + z * stride;
            double surfaceM = terrainHeightBlocks(hm, hmW, hmH, wx, wz);
            int surface = (int)std::llround(surfaceM);

            // Snap the terrain top to the NEAREST stride grid line (not ceil). This keeps a
            // coarse chunk's surface within stride/2 of the true height instead of letting it
            // float up to the next boundary, which is what made distant ground look raised.
            int snapTop = (int)std::llround((double)surface / (double)stride) * stride;
            // A column is underwater when its RENDERED top sits below the sea surface, so the
            // water mesh hugs the exact shoreline of the terrain the player sees at this LOD.
            underwater[x][z] = (snapTop < SEA_LEVEL);
            topSolidY[x][z] = -1;

            // Surface material flows with elevation (sand -> grass -> dirt -> stone -> snow),
            // dithered so the bands fade into each other; submerged columns get a sandy seabed.
            int surfMat = underwater[x][z] ? SAND : surfaceBlock(surfaceM, wx, wz);

            for (int y = 0; y < CHUNK_SIZE; ++y) {
                int voxTop = origin.y + (y + 1) * stride;  // world-y of this voxel's top face
                int type = AIR;
                if (voxTop <= snapTop) {
                    // Solid ground. The topmost solid voxel wears the surface material; every
                    // voxel below it is rock (cliffs and the chunk interior read as stone).
                    bool topVoxel = (voxTop == snapTop);
                    type = topVoxel ? surfMat : STONE;
                }
                voxels[x][y][z] = type;
                if (type) { empty = false; topSolidY[x][z] = y; }
            }
        }
    }

    // Mirror the solid-voxel grid into the chunk's block store so collision/raycasts/edits
    // see the same world the mesh shows (only filled at LOD 0; water/skirts are visual-only).
    if (outBlocks) {
        outBlocks->resize(CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE);
        for (int x = 0; x < CHUNK_SIZE; ++x)
        for (int y = 0; y < CHUNK_SIZE; ++y)
        for (int z = 0; z < CHUNK_SIZE; ++z)
            (*outBlocks)[(x * CHUNK_SIZE + y) * CHUNK_SIZE + z] = Block{ (uint8_t)voxels[x][y][z], 0 };
    }

    // Face meshing on the macro-voxel grid; each emitted face is a stride-sized cube
    // instance (model = translate(voxel centre) * scale(stride)).
    const float centre = 0.5f * (float)(stride - 1);
    for (int x = 0; x < CHUNK_SIZE; ++x)
    for (int y = 0; y < CHUNK_SIZE; ++y)
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        if (voxels[x][y][z] == 0) continue;
        for (int f = 0; f < 6; ++f) {
            glm::ivec3 n = glm::ivec3(x, y, z) + faceOffsets[f];
            if (isInBounds(n.x, n.y, n.z, CHUNK_SIZE) && voxels[n.x][n.y][n.z] != 0) continue;
            glm::vec2 atlasOffset = blockTable[voxels[x][y][z]].tiles[f];
            InstanceKey key = { static_cast<Face>(f), atlasOffset };
            InstanceData data;
            glm::vec3 voxelCentreLocal = glm::vec3(x, y, z) * (float)stride + centre;
            data.model = glm::scale(
                glm::translate(glm::mat4(1.0f), voxelCentreLocal),
                glm::vec3((float)stride));
            data.atlasOffset = atlasOffset;
            out[key].push_back(data);
        }
    }

    // Sea surface mesh (drawn by the separate animated water pass, not the terrain pass):
    // two triangles per underwater column, with nodes on block corners so neighbouring
    // cells share positions and the wavy surface stays watertight. Nodes that touch a land
    // column get shore = 0, pinning the water's edge to the terrain while open water bobs.
    if (chunkHasSea && outWater) {
        // Wet-ness per column with a one-cell border into the neighbouring chunks, so the
        // shore pinning on a chunk's edge matches what the neighbour computes for itself.
        // wet[cx + 1][cz + 1] is column (cx, cz); border columns are sampled directly.
        bool wet[CHUNK_SIZE + 2][CHUNK_SIZE + 2];
        for (int cx = -1; cx <= CHUNK_SIZE; ++cx)
        for (int cz = -1; cz <= CHUNK_SIZE; ++cz) {
            if (cx >= 0 && cx < CHUNK_SIZE && cz >= 0 && cz < CHUNK_SIZE) {
                wet[cx + 1][cz + 1] = underwater[cx][cz];
                continue;
            }
            double s = terrainHeightBlocks(hm, hmW, hmH,
                                           origin.x + cx * stride, origin.z + cz * stride);
            // Same rounding + snapping as the terrain loop above, so seams agree exactly.
            int snap = (int)std::llround((double)(int)std::llround(s) / (double)stride) * stride;
            wet[cx + 1][cz + 1] = (snap < SEA_LEVEL);
        }

        // Shore pin factor per corner node: 1 only when all four touching cells are water.
        float shore[CHUNK_SIZE + 1][CHUNK_SIZE + 1];
        for (int gx = 0; gx <= CHUNK_SIZE; ++gx)
        for (int gz = 0; gz <= CHUNK_SIZE; ++gz)
            shore[gx][gz] = (wet[gx][gz] && wet[gx + 1][gz] &&
                             wet[gx][gz + 1] && wet[gx + 1][gz + 1]) ? 1.0f : 0.0f;

        // Cubes are rendered centred on their coordinates, so the corners of column cx sit
        // at cx * stride -/+ 0.5. Winding is CCW seen from above (matches the Top face).
        for (int cx = 0; cx < CHUNK_SIZE; ++cx)
        for (int cz = 0; cz < CHUNK_SIZE; ++cz) {
            if (!underwater[cx][cz]) continue;
            float x0 = cx * (float)stride - 0.5f, x1 = x0 + (float)stride;
            float z0 = cz * (float)stride - 0.5f, z1 = z0 + (float)stride;
            WaterVertex v00{ x0, z0, shore[cx][cz] };
            WaterVertex v10{ x1, z0, shore[cx + 1][cz] };
            WaterVertex v01{ x0, z1, shore[cx][cz + 1] };
            WaterVertex v11{ x1, z1, shore[cx + 1][cz + 1] };
            outWater->verts.insert(outWater->verts.end(), { v01, v11, v10, v10, v00, v01 });
        }
        if (!outWater->verts.empty()) empty = false;  // a pure-sea chunk still gets drawn
    }

    // Skirts: a vertical curtain hanging down from each chunk-edge column, hiding the cracks
    // that show through where this chunk's surface meets a neighbour at a different height/LOD.
    // The curtain is buried inside same-LOD neighbours (invisible) and only shows at seams.
    const float skirtDepth = (float)(SKIRT_VOX * stride);
    auto addSkirt = [&](int x, int z, int faceDir) {
        if (topSolidY[x][z] < 0) return;
        int ty = topSolidY[x][z];
        float topLocalY = (ty + 1) * (float)stride - 0.5f;   // world-local y of the column's top
        glm::vec2 tile = blockTable[voxels[x][ty][z]].tiles[faceDir];
        InstanceKey key = { static_cast<Face>(faceDir), tile };
        InstanceData data;
        // Cube spanning the column in X/Z (stride) and skirtDepth in Y, top flush with surface.
        // Nudge a hair outward so the curtain doesn't z-fight the column's existing edge wall.
        glm::vec3 outward = glm::vec3(faceOffsets[faceDir]) * (0.03f * (float)stride);
        glm::vec3 centreLocal = glm::vec3(
            x * stride + centre,
            topLocalY - 0.5f * skirtDepth,
            z * stride + centre) + outward;
        data.model = glm::scale(
            glm::translate(glm::mat4(1.0f), centreLocal),
            glm::vec3((float)stride, skirtDepth, (float)stride));
        data.atlasOffset = tile;
        out[key].push_back(data);
    };
    for (int i = 0; i < CHUNK_SIZE; ++i) {
        addSkirt(0, i, Left);                 // -x edge
        addSkirt(CHUNK_SIZE - 1, i, Right);   // +x edge
        addSkirt(i, 0, Back);                 // -z edge
        addSkirt(i, CHUNK_SIZE - 1, Front);   // +z edge
    }

    worldMin = glm::vec3(origin);
    worldMax = glm::vec3(origin + span);
}