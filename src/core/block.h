#pragma once

#include <cstdint>
#include <iterator>

#include <glm/glm.hpp>

#include "core/geometry.h"
#include "core/rotation.h"

// ---------------------------------------------------------------------------------------
// Block data comes in three layers that live in different places:
//   1. static definition  -- one BlockData per TYPE, immutable (this file's blockTable)
//   2. voxel grid         -- one 2-byte Block per CELL (Chunk::blockData / Construct::blockData)
//   3. block entity       -- dynamic state, only for the cells that are machines (sim/entity.h)
// Terrain never touches layer 3. docs/BLOCKS.md walks through adding a block.
// ---------------------------------------------------------------------------------------

// Effect: the face an Additional block touches a Machine through (both sides must be Effect).
enum class PortMode : uint8_t { None, Input, Output, Effect, Any, World };

// What a block DOES. A new kind of behaviour is a new entry here plus a case in
// sim/factory.cpp (advance / gather) and sim/entity.cpp (the item protocol).
enum class BlockBehavior : uint8_t {
    None,        // terrain: no entity, no ports
    Storage,     // passive item store; transport blocks push into and pull from it
    Conveyor,    // belt: positioned items, pushes ahead, pulls from a passive block behind
    Splitter,    // small buffer that round-robins its Output faces
    Pipe,        // fluid belt: bulk units, pushes ahead, pulls from a passive block behind
    Tank,        // passive fluid store
    Machine,     // crafts recipes (sim/recipe.h); passive on the item network
    Additional,  // grants an Effect to a Machine through matching Effect ports (sim/logic.cpp)
    PortIn,      // test-rig source: spawns its chosen item and pushes it out
    PortOut,     // test-rig sink: consumes (and counts) the item it is set to accept
    Compute,
    Sensor,
    Drill,
};

// Local-face bitmasks (geometry.h order) for BlockData::variantFaces.
constexpr uint8_t faceBit(int f) { return (uint8_t)(1u << f); }
constexpr uint8_t FACES_NONE  = 0;
constexpr uint8_t FACES_SIDES = faceBit(Front) | faceBit(Back) | faceBit(Left) | faceBit(Right);
constexpr uint8_t FACES_ALL   = 0x3F;

// Layer 1. `tiles` and `ports` are indexed by LOCAL face in the geometry.h order
// Front(+Z) Back(-Z) Left(-X) Right(+X) Top(+Y) Bottom(-Y); rotation maps them into the
// world (see rotation.h). Machines are authored flowing along local +Y: Bottom in, Top out.
struct BlockData {
    const char* name;
    glm::ivec2  tiles[6];
    PortMode    ports[6] = {};                       // all None unless listed
    BlockBehavior behavior = BlockBehavior::None;
    uint8_t  slotCount      = 0;                     // inventory slots; Machines size theirs from their recipes
    uint16_t slotCapacity   = 0;                     // items / fluid units per slot (Machines: 0 = 64)
    float    itemsPerSecond = 0.0f;                  // transport throughput; Machines: crafting speed multiplier
    bool     ticks          = false;                 // does the sim need to visit it every step?
    bool     fluid          = false;                 // carries LIQUID items only (pipes, tanks, pipe ports)
    uint8_t  variantFaces   = FACES_NONE;            // faces whose tile.x shifts by the entity's visual
                                                     // variant (fluid blocks: the colour of slot 0's fluid)

    bool hasEntity() const { return behavior != BlockBehavior::None; }
};

// Layer 2: the per-cell voxel. Two bytes; allocated for every cell in the world, air included.
struct Block {
    uint8_t type     = 0;   // BlockType (256 types)
    uint8_t rotation = 0;   // 0-23, index into blockRotations / kRot
};
static_assert(sizeof(Block) == 2, "Block must stay a 2-byte cell");

enum BlockType : uint8_t {
    AIR = 0,
    GRASS = 1,
    DIRT = 2,
    SAND = 3,
    STONE = 4,
    WATER = 5,
    SNOWY_DIRT = 6,
    SNOW = 7,
    BARREL = 8,
    CONVEYER_BELT = 9,
    CONVEYER_VAULT = 10,
    CONVEYER_SPLITTER = 11,
    COMPUTE_CHIP = 12,
    SENSOR_VISUAL = 13,
    PORT_IN = 14,
    PORT_OUT = 15,
    MAGNET = 16,
    SIFTER = 17,
    GRINDER = 18,
    PIPE_PLUMBING = 19,
    PIPE_TANK = 20,
    PIPE_PORT_IN = 21,
    PIPE_PORT_OUT = 22,
    DRILL = 23,

    BLOCK_TYPE_COUNT
};

// Short port names so the table below stays readable.
namespace pm {
    constexpr PortMode N = PortMode::None;
    constexpr PortMode I = PortMode::Input;
    constexpr PortMode O = PortMode::Output;
    constexpr PortMode A = PortMode::Any;
    constexpr PortMode E = PortMode::Effect;
    constexpr PortMode W = PortMode::World;
}

// Bottom for input, top for output, sides for utils. This is to make art and programming easier, although it should have front as input and back as output

// tiles:  Front     Back      Left      Right     Top       Bottom
// ports:  Front     Back      Left      Right     Top       Bottom
// then:   behavior, slots, capacity, itemsPerSecond, ticks [, fluid, variantFaces]
inline const BlockData blockTable[] = {
    // AIR / NULL
    { "Air",        { {1,0},   {1,0},   {1,0},   {1,0},   {1,0},   {1,0}   } },

    // GRASS
    { "Grass",      { {1,15},  {1,15},  {1,15},  {1,15},  {2,15},  {0,15}  } },

    // DIRT
    { "Dirt",       { {0,15},  {0,15},  {0,15},  {0,15},  {0,15},  {0,15}  } },

    // SAND
    { "Sand",       { {4,15},  {4,15},  {4,15},  {4,15},  {4,15},  {4,15}  } },

    // STONE
    { "Stone",      { {3,15},  {3,15},  {3,15},  {3,15},  {3,15},  {3,15}  } },

    // WATER
    { "Water",      { {7,15},  {7,15},  {7,15},  {7,15},  {7,15},  {7,15}  } },

    // SNOWY DIRT
    { "Snowy Dirt", { {6,15},  {6,15},  {6,15},  {6,15},  {5,15},  {2,15}  } },

    // SNOW
    { "Snow",       { {5,15},  {5,15},  {5,15},  {5,15},  {5,15},  {5,15}  } },

    // Barrel: passive store, reachable from every side.
    { "Barrel",     { {9,15},  {9,15},  {9,15},  {9,15},  {8,15},  {8,15}  },
                    { pm::A,   pm::A,   pm::A,   pm::A,   pm::A,   pm::A   },
      BlockBehavior::Storage, 1, 64, 0.0f, false },

    // Conveyer Belt: flows Bottom -> Top; the four sides accept side-loading.
    // 8 items/s at BELT_CAPACITY items per cell == 2 cells/s of belt travel.
    { "Conveyer Belt", { {10,15}, {10,15}, {10,15}, {10,15}, {12,15}, {11,15} },
                       { pm::I,   pm::I,   pm::I,   pm::I,   pm::O,   pm::I   },
      BlockBehavior::Conveyor, 0, 0, 8.0f, true },

    // Conveyer Vault: inline buffer, in at the bottom, out at the top.
    { "Conveyer Vault", { {14,15}, {14,15}, {14,15}, {14,15}, {14,15}, {14,15} },
                        { pm::A,   pm::A,   pm::A,   pm::A,   pm::A,   pm::A   },
      BlockBehavior::Storage, 4, 64, 0.0f, false },

    // Conveyer Splitter: in at the bottom, round-robin out of Top / Left / Right.
    // GOING TO GET DELETED
    { "OLD: Conveyer Splitter", { {13,15}, {13,15}, {12,15}, {12,15}, {12,15}, {11,15} },
                           { pm::N,   pm::N,   pm::O,   pm::O,   pm::O,   pm::I   },
      BlockBehavior::Splitter, 1, 4, 8.0f, true },

    // Compute Chip (logic state comes later; no item ports).
    { "Compute Chip", { {8,14},  {8,14},  {8,14},  {8,14},  {8,14},  {8,14}  },
                      { pm::N,   pm::N,   pm::N,   pm::N,   pm::N,   pm::N   },
      BlockBehavior::Compute, 0, 0, 0.0f, false },

    // Sensor Visual: eyes on Top, connector on Bottom (no item ports).
    { "Sensor Visual", { {10,14}, {10,14}, {10,14}, {10,14}, {9,14},  {11,14} },
                       { pm::N,   pm::N,   pm::N,   pm::N,   pm::N,   pm::N   },
      BlockBehavior::Sensor, 0, 0, 0.0f, false },

    // Port In: a component's input feed. In the room it spawns the item it is set to
    // (menu) and emits it from its Back face; its ID is an upper-case letter.
    { "Port In",    { {15,15}, {15,15}, {15,15}, {15,15}, {12,15}, {15,15} },
                    { pm::N,   pm::N,   pm::N,   pm::N,   pm::O,   pm::N   },
      BlockBehavior::PortIn, 1, 4, 8.0f, true },

    // Port Out: a component's output. Accepts the item it is set to (or anything when
    // unset) on its Front face and consumes it, counting deliveries; ID is a lower-case letter.
    { "Port Out",   { {15,15}, {15,15}, {15,15}, {15,15}, {15,15}, {11,15} },
                    { pm::N,   pm::N,   pm::N,   pm::N,   pm::N,   pm::I   },
      BlockBehavior::PortOut, 0, 0, 0.0f, true },

    // Magnet: touches a machine with its Bottom (Effect) face; what it grants is decided in
    // effectOf() in sim/logic.cpp (currently: the MAGNETIC flag and a little speed).
    { "Magnet",     { {9,12},  {9,12},  {9,12},  {9,12},  {8,12},  {10,12} },
                    { pm::N,   pm::N,   pm::N,   pm::N,   pm::N,   pm::E   },
      BlockBehavior::Additional, 0, 0, 0.0f, false },

    // Sifter: Gravel in at the Bottom, fines out the Top, the heavy fraction out the Back;
    // an Effect port on its Front. itemsPerSecond is its crafting speed multiplier.
    { "Sifter",     { {11,13}, {8,13},  {9,13},  {10,13}, {12,15}, {11,15} },
                    { pm::O,   pm::E,   pm::N,   pm::N,   pm::O,   pm::I   },
      BlockBehavior::Machine, 0, 0, 2.0f, true },

    // Grinder: in at the Bottom, out the Top (recipes in sim/recipe.h).
    { "Grinder",    { {14,13}, {14,13}, {14,13}, {14,13}, {13,13}, {12,13} },
                    { pm::N,   pm::N,   pm::N,   pm::N,   pm::O,   pm::I   },
      BlockBehavior::Machine, 0, 0, 1.0f, true },

    // Pipe Plumbing: a fluid belt flowing Bottom -> Top, 8 units/s, buffering 8 units. Its
    // side tiles (10,11) shift right by the fluid it carries: water, oil, mud, slag, molten.
    { "Pipe Plumbing", { {10,11}, {10,11}, {10,11}, {10,11}, {9,11},  {8,11}  },
                       { pm::N,   pm::N,   pm::N,   pm::N,   pm::O,   pm::I   },
      BlockBehavior::Pipe, 1, 8, 8.0f, true, true, FACES_SIDES },

    // Pipe Tank: passive fluid store reachable from every side; every face shows its fluid.
    { "Pipe Tank",  { {10,10}, {10,10}, {10,10}, {10,10}, {10,10}, {10,10} },
                    { pm::A,   pm::A,   pm::A,   pm::A,   pm::A,   pm::A   },
      BlockBehavior::Tank, 4, 64, 0.0f, false, true, FACES_ALL },

    // Pipe Port In / Out: the fluid twins of Port In / Out (their item list is fluids only).
    { "Pipe Port In",  { {9,10},  {9,10},  {9,10},  {9,10},  {9,11},  {9,10}  },
                       { pm::N,   pm::N,   pm::N,   pm::N,   pm::O,   pm::N   },
      BlockBehavior::PortIn, 1, 8, 8.0f, true, true },

    { "Pipe Port Out", { {9,10},  {9,10},  {9,10},  {9,10},  {9,10},  {8,11}  },
                       { pm::N,   pm::N,   pm::N,   pm::N,   pm::N,   pm::I   },
      BlockBehavior::PortOut, 0, 0, 0.0f, true, true },

    { "Drill", { {11,12},  {11,12},  {11,12},  {11,12},  {12,15},  {12,12}  },
                       { pm::N,   pm::N,   pm::N,   pm::N,   pm::O,   pm::W   },
      BlockBehavior::Drill, 0, 0, 0.0f, true, true },
};
static_assert(std::size(blockTable) == BLOCK_TYPE_COUNT, "blockTable and BlockType drifted apart");

inline bool portAccepts(PortMode m) { return m == PortMode::Input  || m == PortMode::Any; }
inline bool portEmits(PortMode m)   { return m == PortMode::Output || m == PortMode::Any; }

// The port a placed block presents on a WORLD face. This is the only place the rest of the
// code needs to know about rotation.
inline PortMode portAt(uint8_t type, uint8_t rotation, Face worldFace) {
    return blockTable[type].ports[kRot.faceMapInv[rotation][worldFace]];
}
inline PortMode portAt(const Block& b, Face worldFace) {
    return portAt(b.type, b.rotation, worldFace);
}

// Validates on the way in: unknown types become air, out-of-range rotations become 0.
inline Block makeBlock(int type, int rotation) {
    Block b;
    b.type     = (type > 0 && type < BLOCK_TYPE_COUNT) ? (uint8_t)type : (uint8_t)AIR;
    b.rotation = isValidRotation(rotation) ? (uint8_t)rotation : 0;
    return b;
}
