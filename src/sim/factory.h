#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "sim/entity.h"

struct Construct;

// ---------------------------------------------------------------------------------------
// The transfer protocol. Two rules keep it from double-moving items:
//   1. Transport blocks (belts, pipes, splitters, ports) push and pull; everything else --
//      storage, tanks, machines -- is passive. A belt pushes into whatever its output faces
//      and pulls from a PASSIVE block behind it, so any given transfer has exactly one owner
//      (between two belts, the upstream one).
//   2. Two-phase tick: every entity first advances its own private state (belt travel,
//      crafting, effect scans), then all transfer intents are gathered read-only, then
//      applied. Nothing can hop five belts in one step.
// A disabled block (PropKey::Enabled = 0) sits the whole tick out.
// ---------------------------------------------------------------------------------------

struct TransferIntent {
    glm::ivec3 from, to;
    ItemId     item;
    uint16_t   count;
};

// Do a's port toward b and b's port toward a agree on a transfer a -> b? Works for any rotation
// of any block; cells must be face-adjacent (otherwise false).
bool canTransfer(const Block& a, glm::ivec3 aPos, const Block& b, glm::ivec3 bPos);
bool canTransfer(const BlockEntity& a, const BlockEntity& b);   // also false if either is disabled

bool isTransport(BlockBehavior b);

struct FactorySim {
    std::vector<Construct*>     targets;   // constructs being simulated; the owner refreshes it each frame
    std::vector<TransferIntent> intents;   // scratch, reused every tick
    float accumulator = 0.0f;
    bool  portsActive = true;              // Port In spawns items (the blueprint room's test rig)

    void update(float frameDt);            // fixed-step driver: runs whole FACTORY_TICK_DT steps
    void tick(float dt);                   // exactly one simulation step

    BlockEntity* find(glm::ivec3 worldPos) const;   // across every target construct
    BlockEntity* findPortById(char id) const;       // 'A'-'Z' finds a Port In, 'a'-'z' a Port Out

private:
    void advance(BlockEntity& e, float dt);   // phase 1: private state (belt travel, budgets, crafting)
    void gather(BlockEntity& e);              // phase 2: read-only over neighbours
    void apply();                             // phase 3: mutate inventories
    void refreshVisuals();                    // flag constructs whose fluid tiles changed

    void pump(BlockEntity& e);                // gather for inventory-style transport (splitter, pipe, ports)
    void machineAdvance(BlockEntity& e, float dt);
};

// One-line-per-item readout for debugging.
std::string describeEntity(const BlockEntity& e);
