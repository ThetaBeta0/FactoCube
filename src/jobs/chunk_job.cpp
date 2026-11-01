#include "jobs/chunk_job.h"

#include "world/chunk.h"

void chunkWorkerThread(TaskQueue& inputQueue, TaskQueue& outputQueue) {
    while (true) {
        ChunkGenerationTask task;
        if (!inputQueue.pop(task)) break;  // Shutdown signaled
        
        try {
            // === CPU-ONLY WORK ===
            // Generate block data + instances at this task's LOD, plus culling bounds.
            std::map<InstanceKey, std::vector<InstanceData>> instances;
            // Editable (LOD 0) chunks also export their voxel grid so blockData can mirror the mesh.
            buildChunkMesh(task.lod, glm::ivec3(task.chunkCoord),
                           task.heightMapData, task.heightmapWidth, task.heightmapHeight,
                           instances, task.worldMin, task.worldMax, task.empty,
                           task.lod == 0 ? &task.blocks : nullptr,
                           &task.water);

            task.instanceGroups = std::move(instances);
            task.success = true;

        } catch (...) {
            task.success = false;  // Handle errors gracefully
        }
        
        outputQueue.push(std::move(task));
    }
}