#include "sim/entity.h"

#include <algorithm>

#include "sim/recipe.h"

namespace {
// A sink takes everything a neighbour can hand over in one step; the pushers are rate-limited.
constexpr uint16_t SINK_ACCEPT = 64;
constexpr uint16_t MACHINE_DEFAULT_CAP = 64;

int localFaceOf(const BlockEntity& e, int worldFace) {
    return worldFace < 0 ? -1 : (int)worldToLocalFace(e.rotation, static_cast<Face>(worldFace));
}
}

BlockEntity makeEntity(glm::ivec3 pos, Block b) {
    BlockEntity e;
    e.pos      = pos;
    e.type     = b.type;
    e.rotation = b.rotation;
    const BlockData& d = blockTable[b.type];
    e.inv.configure(d.slotCount, d.slotCapacity);
    switch (d.behavior) {
        case BlockBehavior::Conveyor: e.ext = ConveyorState{}; break;
        case BlockBehavior::Splitter: e.ext = SplitterState{}; break;
        case BlockBehavior::PortIn:
        case BlockBehavior::PortOut:  e.ext = PortState{};     break;
        case BlockBehavior::Machine: {
            // Slot rows come from the recipes: one input slot per input of the biggest recipe,
            // likewise for outputs. The table's capacity applies per slot (0 = default).
            MachineState ms;
            uint16_t cap = d.slotCapacity ? d.slotCapacity : MACHINE_DEFAULT_CAP;
            e.inv.configure((uint8_t)std::max(1, recipeMaxIO(b.type, false)), cap);
            ms.out.configure((uint8_t)std::max(1, recipeMaxIO(b.type, true)), cap);
            ms.rng = 0x9E3779B9u ^ (uint32_t)(pos.x * 73856093) ^ (uint32_t)(pos.y * 19349663)
                   ^ (uint32_t)(pos.z * 83492791);
            if (ms.rng == 0) ms.rng = 1;
            e.ext = ms;
            break;
        }
        default: break;
    }
    return e;
}

uint8_t visualVariant(const BlockEntity& e) {
    if (e.def().fluid) return fluidVariant(e.inv.firstItem());
    return 0;
}

uint16_t BlockEntity::acceptable(ItemId id, int worldFace) const {
    if (id == ItemId::None || !enabled()) return 0;
    const BlockData& d = def();
    if (d.fluid && !isFluid(id)) return 0;
    switch (d.behavior) {
        case BlockBehavior::Conveyor:
            return std::get<ConveyorState>(ext).hasClearance() ? 1 : 0;
        case BlockBehavior::PortOut: {
            ItemId filter = portItem();
            return (filter == ItemId::None || filter == id) ? SINK_ACCEPT : 0;
        }
        case BlockBehavior::Machine:
            // Only what some recipe takes through this face, into the input side.
            return recipeTakesOn(type, id, localFaceOf(*this, worldFace)) ? inv.acceptable(id) : 0;
        default:
            return inv.acceptable(id);
    }
}

uint16_t BlockEntity::insert(ItemId id, uint16_t n) {
    if (n == 0 || id == ItemId::None || !enabled()) return 0;
    const BlockData& d = def();
    if (d.fluid && !isFluid(id)) return 0;
    switch (d.behavior) {
        case BlockBehavior::Conveyor: {
            auto& c = std::get<ConveyorState>(ext);
            if (!c.hasClearance()) return 0;
            c.pushBack(id);
            return 1;
        }
        case BlockBehavior::PortOut: {
            // Consumed on arrival, not stored; the count is what the rig reports.
            if (acceptable(id) == 0) return 0;
            std::get<PortState>(ext).count += n;
            return n;
        }
        case BlockBehavior::Machine:
            return recipeTakesOn(type, id, -1) ? inv.insert(id, n) : 0;
        default:
            return inv.insert(id, n);
    }
}

uint16_t BlockEntity::extract(ItemId id, uint16_t n) {
    if (n == 0 || id == ItemId::None) return 0;
    switch (behavior()) {
        case BlockBehavior::Conveyor: {
            // A belt only ever gives up its front item, and only once it has reached the exit.
            auto& c = std::get<ConveyorState>(ext);
            if (!c.frontReady() || c.at(0).item != id) return 0;
            c.popFront();
            return 1;
        }
        case BlockBehavior::PortOut:
            return 0;
        case BlockBehavior::Machine:
            return std::get<MachineState>(ext).out.extract(id, n);   // only finished goods leave
        default:
            return inv.extract(id, n);
    }
}

uint16_t BlockEntity::available(ItemId id) const {
    if (id == ItemId::None || !enabled()) return 0;
    switch (behavior()) {
        case BlockBehavior::Conveyor: {
            auto& c = std::get<ConveyorState>(ext);
            return (c.frontReady() && c.at(0).item == id) ? 1 : 0;
        }
        case BlockBehavior::PortOut:  return 0;
        case BlockBehavior::Machine:  return std::get<MachineState>(ext).out.count(id);
        default:                      return inv.count(id);
    }
}

ItemId BlockEntity::firstItem() const {
    switch (behavior()) {
        case BlockBehavior::Conveyor: {
            auto& c = std::get<ConveyorState>(ext);
            return c.count ? c.at(0).item : ItemId::None;
        }
        case BlockBehavior::PortOut:
            return ItemId::None;
        case BlockBehavior::Machine: {
            ItemId id = std::get<MachineState>(ext).out.firstItem();
            return id != ItemId::None ? id : inv.firstItem();
        }
        default:
            return inv.firstItem();
    }
}

uint16_t BlockEntity::itemCount() const {
    switch (behavior()) {
        case BlockBehavior::Conveyor: return std::get<ConveyorState>(ext).count;
        case BlockBehavior::PortOut:  return 0;
        case BlockBehavior::Machine:  return inv.total() + std::get<MachineState>(ext).out.total();
        default:                      return inv.total();
    }
}

ItemId BlockEntity::offerFor(const BlockEntity& dst, int myWorldFace, int dstWorldFace) const {
    if (!enabled()) return ItemId::None;
    switch (behavior()) {
        case BlockBehavior::Conveyor: {
            auto& c = std::get<ConveyorState>(ext);
            if (!c.frontReady()) return ItemId::None;
            ItemId id = c.at(0).item;
            return dst.acceptable(id, dstWorldFace) > 0 ? id : ItemId::None;
        }
        case BlockBehavior::PortOut:
            return ItemId::None;
        case BlockBehavior::Machine: {
            // Finished goods leave only through the face the recipe bound them to.
            const Inventory& out = std::get<MachineState>(ext).out;
            int lf = localFaceOf(*this, myWorldFace);
            for (int i = 0; i < out.slotCount; ++i) {
                const ItemStack& st = out.slots[i];
                if (st.empty() || !recipeOffersOn(type, st.item, lf)) continue;
                if (dst.acceptable(st.item, dstWorldFace) > 0) return st.item;
            }
            return ItemId::None;
        }
        default:
            for (int i = 0; i < inv.slotCount; ++i) {
                const ItemStack& st = inv.slots[i];
                if (!st.empty() && dst.acceptable(st.item, dstWorldFace) > 0) return st.item;
            }
            return ItemId::None;
    }
}
