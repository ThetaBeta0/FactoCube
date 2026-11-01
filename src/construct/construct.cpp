#include "construct/construct.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "core/block.h"
#include "core/geometry.h"

void Construct::updateBounds() {
    // The grid's local (0,0,0) sits at `origin` in world blocks; bounds are just
    // the grid extent. Rendering offsets by (worldMin - camera) so the construct
    // shares the chunks' floating-origin / camera-relative pipeline.
    worldMin = glm::vec3(origin);
    worldMax = glm::vec3(origin + size);
}

void Construct::genInstances() {
    for (int x = 0; x < size.x; ++x) {
        for (int y = 0; y < size.y; ++y) {
            for (int z = 0; z < size.z; ++z) {
                if (blockData[x][y][z].type == AIR) continue;

                // The block's rotation (0-23) spins all six faces together: each face's
                // geometry, tile and outward normal rotate as one. Cull is therefore done
                // against the neighbour the *rotated* face actually points at (kRot.faceMap),
                // not its unrotated direction -- otherwise rotated blocks open faces in wrong spots.
                unsigned int rot = blockData[x][y][z].rotation % blockRotations.size();
                const glm::mat4& R = blockRotations[rot];
                const BlockData& def = blockTable[blockData[x][y][z].type];

                // Blocks that show their contents (fluid pipes / tanks) shift the tile column of
                // their variant faces by the variant the sim last settled on (visualKey).
                uint8_t variant = 0;
                if (def.variantFaces)
                    if (const BlockEntity* e = entityAt(origin + glm::ivec3(x, y, z)))
                        variant = e->visualKey;

                for (int f = 0; f < 6; ++f) {
                    glm::ivec3 neighbor = glm::ivec3(x, y, z) + faceOffsets[kRot.faceMap[rot][f]];
                    if (isInSize(neighbor.x, neighbor.y, neighbor.z, size) && blockData[neighbor.x][neighbor.y][neighbor.z].type != AIR) continue;
                    glm::ivec2 tile = def.tiles[f];
                    if (variant && (def.variantFaces & faceBit(f))) tile.x += variant;
                    glm::vec2 atlasOffset = glm::vec2(tile.x, tile.y);
                    Face face = static_cast<Face>(f);
                    InstanceKey key = {face, atlasOffset};
                    InstanceData data;

                    // Grid-local position only; the construct's world origin is applied
                    // (camera-relative) in the shader for floating-origin precision.
                    data.model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)) * R;
                    data.atlasOffset = atlasOffset;

                    instanceGroups[key].push_back(data);
                }
            }
        }
    }
}

void Construct::draw() {
    instanceVBO.draw();
}

bool Construct::contains(glm::ivec3 wp) const {
    glm::ivec3 l = wp - origin;
    return l.x >= 0 && l.x < size.x &&
           l.y >= 0 && l.y < size.y &&
           l.z >= 0 && l.z < size.z;
}

bool Construct::isSolidAt(glm::ivec3 wp) const {
    const Block* b = blockAt(wp);
    return b && b->type != AIR;
}

const Block* Construct::blockAt(glm::ivec3 wp) const {
    if (!contains(wp)) return nullptr;
    glm::ivec3 l = wp - origin;
    return &blockData[l.x][l.y][l.z];
}

BlockEntity* Construct::entityAt(glm::ivec3 wp) {
    auto it = entityIndex.find(wp);
    return it == entityIndex.end() ? nullptr : &entities[it->second];
}

const BlockEntity* Construct::entityAt(glm::ivec3 wp) const {
    auto it = entityIndex.find(wp);
    return it == entityIndex.end() ? nullptr : &entities[it->second];
}

void Construct::removeEntity(glm::ivec3 wp) {
    auto it = entityIndex.find(wp);
    if (it == entityIndex.end()) return;
    uint32_t i = it->second, last = (uint32_t)entities.size() - 1;
    if (i != last) {
        // Swap-and-pop keeps the vector dense; re-point the moved entity's index entry.
        entities[i] = std::move(entities[last]);
        entityIndex[entities[i].pos] = i;
    }
    entities.pop_back();
    entityIndex.erase(wp);
}

int Construct::distanceTo(glm::ivec3 wp) const {
    if (empty()) return INT_MAX;
    glm::ivec3 lo = origin;
    glm::ivec3 hi = origin + size - 1;          // inclusive far cell
    int dx = std::max({lo.x - wp.x, 0, wp.x - hi.x});
    int dy = std::max({lo.y - wp.y, 0, wp.y - hi.y});
    int dz = std::max({lo.z - wp.z, 0, wp.z - hi.z});
    return std::max({dx, dy, dz});             // Chebyshev distance to the box (0 if inside)
}

void Construct::setBlock(glm::ivec3 wp, Block b) {
    if (empty()) {
        // First block: spawn a 1x1x1 grid anchored at wp.
        origin = wp;
        size = glm::ivec3(1);
        blockData.assign(1, std::vector<std::vector<Block>>(1, std::vector<Block>(1, Block{})));
    } else {
        // Grow/shift the grid so it encloses both its current extent and wp. Entities are
        // keyed by world position, so they are untouched by the grid moving under them.
        glm::ivec3 lo = glm::min(origin, wp);
        glm::ivec3 hi = glm::max(origin + size, wp + glm::ivec3(1));
        glm::ivec3 newSize = hi - lo;

        if (lo != origin || newSize != size) {
            std::vector<std::vector<std::vector<Block>>> grown(
                newSize.x, std::vector<std::vector<Block>>(
                    newSize.y, std::vector<Block>(newSize.z, Block{})));
            glm::ivec3 off = origin - lo;          // where the old grid lands in the new one
            for (int x = 0; x < size.x; ++x)
                for (int y = 0; y < size.y; ++y)
                    for (int z = 0; z < size.z; ++z)
                        grown[x + off.x][y + off.y][z + off.z] = blockData[x][y][z];
            blockData = std::move(grown);
            origin = lo;
            size = newSize;
        }
    }

    glm::ivec3 l = wp - origin;
    blockData[l.x][l.y][l.z] = b;

    // Layer 3 follows the cell: whatever was here loses its state, and a machine gets fresh state.
    removeEntity(wp);
    if (blockTable[b.type].hasEntity()) {
        entityIndex[wp] = (uint32_t)entities.size();
        entities.push_back(makeEntity(wp, b));
    }
}

void Construct::rebuild() {
    updateBounds();
    instanceGroups.clear();
    genInstances();
    instanceVBO.upload(instanceGroups);  // push the regenerated mesh to the GPU
    instanceGroups.clear();              // free the CPU-side copy (mirrors the initial load)
}