#pragma once

#include <map>
#include <vector>

#include <glm/glm.hpp>

#include "core/block.h"
#include "jobs/thread_safe_queue.h"
#include "render/instance.h"
#include "render/water.h"

struct ChunkGenerationTask {
    glm::vec3 chunkCoord;
    int lod = 0;                    // LOD level: macro-voxel size = 2^lod blocks
    double priority = 0.0;          // lower = generated sooner (camera distance at request)
    unsigned char* heightMapData;  // Can be shared (read-only)
    int heightmapWidth;
    int heightmapHeight;
    bool empty = false;

    // Results (filled by worker)
    bool success = false;
    std::map<InstanceKey, std::vector<InstanceData>> instanceGroups;
    std::vector<Block> blocks;     // LOD-0 voxel grid (flattened) for collision/edits; empty otherwise
    WaterMeshData water;           // sea-surface mesh (non-empty only in the sea-cap chunk)
    glm::vec3 worldMin, worldMax;  // For frustum culling
};

struct TaskPriorityCompare {
    bool operator()(const ChunkGenerationTask& a, const ChunkGenerationTask& b) const {
        return a.priority > b.priority;
    }
};

using TaskQueue = ThreadSafeQueue<ChunkGenerationTask, TaskPriorityCompare>;

void chunkWorkerThread(TaskQueue& inputQueue, TaskQueue& outputQueue);