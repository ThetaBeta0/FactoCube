#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "core/block.h"
#include "core/constants.h"
#include "core/inventory.h"
#include "sim/logic.h"

// Layer 3: dynamic per-cell state, allocated only for the cells that are machines. Common
// fields live inline; behaviour-specific extras sit in a variant (no virtuals -- the whole
// thing stays trivially copyable and cache-friendly).

constexpr float BELT_SPACING = 1.0f / (float)BELT_CAPACITY;   // minimum gap between belt items (cells)

struct BeltItem {
    ItemId item     = ItemId::None;
    float  progress = 0.0f;   // 0 = just entered the cell, 1 = at the exit face, ready to hand off
};

// Ring buffer of the items riding one belt cell; at(0) is the frontmost (first to leave).
// Items only advance while the one ahead leaves them BELT_SPACING of clearance, so a blocked
// belt compresses into a queue instead of overlapping.
struct ConveyorState {
    BeltItem items[BELT_CAPACITY];
    uint8_t  head  = 0;   // index of the frontmost item
    uint8_t  count = 0;

    BeltItem&       at(int i)       { return items[(head + i) % BELT_CAPACITY]; }
    const BeltItem& at(int i) const { return items[(head + i) % BELT_CAPACITY]; }

    bool hasClearance() const {   // room for one more at the entry
        return count < BELT_CAPACITY && (count == 0 || at(count - 1).progress >= BELT_SPACING);
    }
    bool frontReady() const { return count > 0 && at(0).progress >= 1.0f; }

    void pushBack(ItemId id) { at(count) = { id, 0.0f }; ++count; }
    ItemId popFront() {
        ItemId id = at(0).item;
        at(0) = BeltItem{};
        head = (uint8_t)((head + 1) % BELT_CAPACITY);
        --count;
        return id;
    }
};

// A Machine's input side is the entity's `inv`; this is everything else it needs.
struct MachineState {
    int16_t   recipe   = -1;     // index into recipeTable while crafting, -1 idle
    float     progress = 0.0f;   // 0..1 through the current craft
    Inventory out;               // finished goods, offered through the recipe's output faces
    uint32_t  crafted  = 0;
    uint32_t  rng      = 1;      // per-machine stream for bonus rolls (deterministic sim)
    Effect    fx;                // what touching Additional blocks grant, refreshed every tick
};
struct SplitterState { uint8_t nextOutput = 0; };   // local face the round-robin resumes from
struct PortState     { uint32_t count = 0; };       // PortIn: items spawned; PortOut: items received

// ---- Sparse per-block properties --------------------------------------------------------
// Settings a player (or, later, a compute chip) puts on a block. Only NON-DEFAULT values are
// stored: a fresh block carries no entries, and setting a property back to its default removes
// the entry again -- so an untouched block costs nothing beyond the (empty) list itself, and a
// future save format can write just these pairs.
enum class PropKey : uint8_t {
    Enabled  = 0,   // 0/1, default 1. A disabled block neither moves, pushes, pulls nor accepts.
    PortItem = 1,   // ItemId a port spawns (In) or filters on (Out); default ItemId::None
    PortId   = 2,   // ASCII letter naming a port: 'A'-'Z' for In, 'a'-'z' for Out; default 0
};

inline uint16_t propDefault(PropKey k) {
    switch (k) {
        case PropKey::Enabled:  return 1;
        case PropKey::PortItem: return (uint16_t)ItemId::None;
        default:                return 0;
    }
}

struct Prop {
    PropKey  key;
    uint16_t value;
};

struct PropList {
    std::vector<Prop> entries;   // a handful at most, so a linear scan beats any indexing

    uint16_t get(PropKey k) const {
        for (const Prop& p : entries) if (p.key == k) return p.value;
        return propDefault(k);
    }
    bool has(PropKey k) const {
        for (const Prop& p : entries) if (p.key == k) return true;
        return false;
    }
    void set(PropKey k, uint16_t v) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].key != k) continue;
            if (v == propDefault(k)) entries.erase(entries.begin() + i);   // back to default: drop it
            else entries[i].value = v;
            return;
        }
        if (v != propDefault(k)) entries.push_back({ k, v });
    }
    bool empty() const { return entries.empty(); }
};

struct BlockEntity {
    glm::ivec3 pos{0};       // world coords, so no back-pointer to the owning grid is needed
    uint8_t    type = 0;
    uint8_t    rotation = 0;
    uint8_t    visualKey = 0; // variant the mesher shows; the sim updates it and flags a re-mesh
    float      rate = 0.0f;  // transfer budget (items / fluid units) for rate-limited blocks
    Inventory  inv;          // items held (Machines: the input side)
    PropList   props;
    std::variant<std::monostate, ConveyorState, MachineState, SplitterState, PortState> ext;

    const BlockData& def() const { return blockTable[type]; }
    BlockBehavior behavior() const { return def().behavior; }
    PortMode portAt(Face worldFace) const { return ::portAt(type, rotation, worldFace); }
    bool isPort() const {
        return behavior() == BlockBehavior::PortIn || behavior() == BlockBehavior::PortOut;
    }

    // Typed views over the sparse props.
    bool   enabled() const            { return props.get(PropKey::Enabled) != 0; }
    void   setEnabled(bool on)        { props.set(PropKey::Enabled, on ? 1 : 0); }
    ItemId portItem() const           { return (ItemId)props.get(PropKey::PortItem); }
    void   setPortItem(ItemId id)     { props.set(PropKey::PortItem, (uint16_t)id); }
    char   portId() const             { return (char)props.get(PropKey::PortId); }
    void   setPortId(char c)          { props.set(PropKey::PortId, (uint16_t)(unsigned char)c); }

    // Item protocol used by the transfer phases. Dispatches on behaviour so a belt (positioned
    // items), a sink (stores nothing) and a machine (input side in, output side out, filtered
    // per face by its recipes) all look the same to callers. A disabled block accepts and
    // offers nothing; a fluid block handles LIQUID items only.
    uint16_t acceptable(ItemId id, int worldFace = -1) const;   // how many could come in through that face
    uint16_t insert(ItemId id, uint16_t n);                     // returns the amount actually inserted
    uint16_t extract(ItemId id, uint16_t n);                    // returns the amount actually removed
    uint16_t available(ItemId id) const;                        // how many of `id` it could hand out
    ItemId   firstItem() const;                                 // first item held (display / probing)
    uint16_t itemCount() const;
    // The first item this block hands out through `myWorldFace` that `dst` will take through
    // `dstWorldFace`, or None. Transport blocks pull with this.
    ItemId   offerFor(const BlockEntity& dst, int myWorldFace, int dstWorldFace) const;
};

// Fresh entity for a just-placed block: inventory sized from the table (or the recipes, for a
// Machine), variant picked by behaviour.
BlockEntity makeEntity(glm::ivec3 pos, Block b);

// Tile-column shift for BlockData::variantFaces: fluid blocks show the fluid in slot 0.
uint8_t visualVariant(const BlockEntity& e);
