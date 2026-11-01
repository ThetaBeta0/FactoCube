#include "sim/factory.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "construct/construct.h"
#include "sim/logic.h"
#include "sim/recipe.h"

bool isTransport(BlockBehavior b) {
    return b == BlockBehavior::Conveyor || b == BlockBehavior::Splitter || b == BlockBehavior::Pipe
        || b == BlockBehavior::PortIn   || b == BlockBehavior::PortOut;
}

bool canTransfer(const Block& a, glm::ivec3 aPos, const Block& b, glm::ivec3 bPos) {
    int outFace = faceFromNormal(bPos - aPos);        // a's face toward b
    if (outFace < 0) return false;                     // not face-adjacent
    Face inFace = oppositeFace(static_cast<Face>(outFace));   // b's face toward a
    return portEmits(portAt(a, static_cast<Face>(outFace)))
        && portAccepts(portAt(b, inFace));
}

bool canTransfer(const BlockEntity& a, const BlockEntity& b) {
    if (!a.enabled() || !b.enabled()) return false;
    return canTransfer(Block{ a.type, a.rotation }, a.pos, Block{ b.type, b.rotation }, b.pos);
}

namespace {

// xorshift32 -> [0,1); deterministic per machine so replays match.
float nextRand(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (float)(s & 0xFFFFFF) / 16777216.0f;
}

} // namespace

void FactorySim::update(float frameDt) {
    accumulator += frameDt;
    int steps = 0;
    while (accumulator >= FACTORY_TICK_DT && steps < FACTORY_MAX_STEPS_PER_FRAME) {
        tick(FACTORY_TICK_DT);
        accumulator -= FACTORY_TICK_DT;
        ++steps;
    }
    if (steps == FACTORY_MAX_STEPS_PER_FRAME) accumulator = 0.0f;   // drop the backlog, don't spiral
}

BlockEntity* FactorySim::find(glm::ivec3 wp) const {
    for (Construct* c : targets)
        if (BlockEntity* e = c->entityAt(wp)) return e;
    return nullptr;
}

BlockEntity* FactorySim::findPortById(char id) const {
    if (id == 0) return nullptr;
    for (Construct* c : targets)
        for (BlockEntity& e : c->entities)
            if (e.isPort() && e.portId() == id) return &e;
    return nullptr;
}

void FactorySim::tick(float dt) {
    for (Construct* c : targets)
        for (BlockEntity& e : c->entities)
            if (e.def().ticks && e.enabled()) advance(e, dt);

    intents.clear();
    for (Construct* c : targets)
        for (BlockEntity& e : c->entities)
            if (e.def().ticks && e.enabled()) gather(e);

    apply();
    refreshVisuals();
}

// ---- phase 1 ------------------------------------------------------------------------------

void FactorySim::advance(BlockEntity& e, float dt) {
    const BlockData& d = e.def();
    switch (d.behavior) {
        case BlockBehavior::Conveyor: {
            auto& cs = std::get<ConveyorState>(e.ext);
            // itemsPerSecond is the belt's throughput; with BELT_CAPACITY items per cell that is
            // itemsPerSecond * BELT_SPACING cells of travel per second.
            float step = d.itemsPerSecond * BELT_SPACING * dt;
            for (int i = 0; i < cs.count; ++i) {
                BeltItem& it = cs.at(i);
                float limit = (i == 0) ? 1.0f : cs.at(i - 1).progress - BELT_SPACING;
                it.progress = std::max(it.progress, std::min(it.progress + step, limit));
            }
            break;
        }
        case BlockBehavior::Splitter:
        case BlockBehavior::Pipe:
        case BlockBehavior::PortIn: {
            // Bank throughput: items up to two at a time (no bursts), fluids up to a full
            // buffer so a pipe can hand over several units in one step.
            float cap = d.fluid ? (float)std::max<int>(d.slotCapacity, 1) : 2.0f;
            e.rate = std::min(e.rate + d.itemsPerSecond * dt, cap);
            // A source keeps its buffer topped up with the chosen item; the push rate above is
            // what actually meters the flow. Off outside the room, so it can't be a free
            // item printer in the world.
            if (d.behavior == BlockBehavior::PortIn && portsActive) {
                ItemId id = e.portItem();
                uint16_t room = (id == ItemId::None) ? 0 : e.acceptable(id);
                if (room) {
                    e.inv.insert(id, room);
                    std::get<PortState>(e.ext).count += room;
                }
            }
            break;
        }
        case BlockBehavior::Machine:
            machineAdvance(e, dt);
            break;
        default:
            break;
    }
}

void FactorySim::machineAdvance(BlockEntity& e, float dt) {
    const BlockData& d = e.def();
    auto& ms = std::get<MachineState>(e.ext);

    // Effects: every Additional block pressing an Effect port against one of ours.
    Effect fx;
    for (int lf = 0; lf < FACE_COUNT; ++lf) {
        if (d.ports[lf] != PortMode::Effect) continue;
        Face wf = localToWorldFace(e.rotation, static_cast<Face>(lf));
        BlockEntity* nb = find(e.pos + faceOffsets[wf]);
        if (!nb || nb->behavior() != BlockBehavior::Additional || !nb->enabled()) continue;
        if (nb->portAt(oppositeFace(wf)) != PortMode::Effect) continue;
        fx = combineEffects(fx, effectOf(*nb, e));
    }
    ms.fx = fx;

    // Idle: start the first recipe whose requirements, inputs and output room are all there.
    if (ms.recipe < 0) {
        for (int i = 0; i < RECIPE_COUNT; ++i) {
            const Recipe& r = recipeTable[i];
            if (r.machine != e.type || !fx.has(r.needs)) continue;
            bool ok = true;
            for (const RecipeIO& io : r.inputs) {
                if (ioEnd(io)) break;
                if (e.inv.count(io.item) < io.count) { ok = false; break; }
            }
            for (const RecipeIO& io : r.outputs) {
                if (!ok || ioEnd(io)) break;
                if (ms.out.acceptable(io.item) < io.count) { ok = false; break; }
            }
            if (!ok) continue;
            for (const RecipeIO& io : r.inputs) {   // consume up front: a craft can't be half-fed
                if (ioEnd(io)) break;
                e.inv.extract(io.item, io.count);
            }
            ms.recipe = (int16_t)i;
            ms.progress = 0.0f;
            break;
        }
    }
    if (ms.recipe < 0) return;

    // Crafting: the block's speed multiplier and the effects' one both divide the recipe time.
    const Recipe& r = recipeTable[ms.recipe];
    float speed = (d.itemsPerSecond > 0.0f ? d.itemsPerSecond : 1.0f) * fx.speed;
    ms.progress = std::min(1.0f, ms.progress + dt * speed / std::max(r.seconds, 0.01f));
    if (ms.progress < 1.0f) return;

    // Done -- but only hand over when every output fits (else hold at 100% until pulled).
    for (const RecipeIO& io : r.outputs) {
        if (ioEnd(io)) break;
        if (ms.out.acceptable(io.item) < io.count) return;
    }
    bool bonus = fx.bonusChance > 0.0f && nextRand(ms.rng) < fx.bonusChance;
    for (const RecipeIO& io : r.outputs) {
        if (ioEnd(io)) break;
        ms.out.insert(io.item, io.count);
        if (bonus) ms.out.insert(io.item, 1);
    }
    ++ms.crafted;
    onCraftComplete(e, r, fx);
    ms.recipe = -1;
    ms.progress = 0.0f;
}

// ---- phase 2 ------------------------------------------------------------------------------
// Gather is read-only over OTHER entities; it may only touch the gathering entity's own
// scheduling state (rate budget, round-robin cursor), which nobody else reads this tick.

void FactorySim::gather(BlockEntity& e) {
    switch (e.behavior()) {
        case BlockBehavior::Conveyor: {
            auto& cs = std::get<ConveyorState>(e.ext);

            // Push the front item out of the exit face once it gets there.
            if (cs.frontReady()) {
                Face wf = localToWorldFace(e.rotation, Top);
                glm::ivec3 np = e.pos + faceOffsets[wf];
                ItemId id = cs.at(0).item;
                BlockEntity* nb = find(np);
                if (nb && canTransfer(e, *nb) && nb->acceptable(id, oppositeFace(wf)) > 0)
                    intents.push_back({ e.pos, np, id, 1 });
            }

            // Pull from a passive block behind the entry face while there's clearance. Another
            // transport block behind us pushes instead (rule 1), so it is skipped here.
            if (cs.hasClearance()) {
                Face wf = localToWorldFace(e.rotation, Bottom);
                glm::ivec3 np = e.pos + faceOffsets[wf];
                BlockEntity* nb = find(np);
                if (nb && !isTransport(nb->behavior()) && canTransfer(*nb, e)) {
                    ItemId id = nb->offerFor(e, oppositeFace(wf), wf);
                    if (id != ItemId::None) intents.push_back({ np, e.pos, id, 1 });
                }
            }
            break;
        }

        case BlockBehavior::Splitter:
        case BlockBehavior::Pipe:
        case BlockBehavior::PortIn:
        case BlockBehavior::PortOut:
            pump(e);
            break;

        default:
            break;   // Storage / Tank / Machine / Additional / Compute / Sensor: passive
    }
}

// Inventory-style transport: push out of the next Output face that can take something (one
// item, or for fluids up to the banked budget of units), then pull whatever a passive
// neighbour offers into an Input face while there is room.
void FactorySim::pump(BlockEntity& e) {
    const BlockData& d = e.def();
    const bool bulk = d.fluid;
    uint16_t budget = bulk ? (uint16_t)std::floor(e.rate) : (e.rate >= 1.0f ? 1 : 0);

    if (budget > 0 && !e.inv.empty()) {
        uint8_t scratch = 0;
        uint8_t& next = (d.behavior == BlockBehavior::Splitter)
            ? std::get<SplitterState>(e.ext).nextOutput : scratch;
        ItemId id = e.inv.firstItem();
        for (int k = 0; k < FACE_COUNT; ++k) {
            Face lf = static_cast<Face>((next + k) % FACE_COUNT);
            if (!portEmits(d.ports[lf])) continue;
            Face wf = localToWorldFace(e.rotation, lf);
            glm::ivec3 np = e.pos + faceOffsets[wf];
            BlockEntity* nb = find(np);
            if (!nb || !canTransfer(e, *nb)) continue;
            uint16_t n = std::min({ budget, e.inv.count(id), nb->acceptable(id, oppositeFace(wf)) });
            if (n == 0) continue;
            intents.push_back({ e.pos, np, id, n });
            next = (uint8_t)((lf + 1) % FACE_COUNT);
            e.rate -= (float)n;
            break;
        }
    }

    for (int lf = 0; lf < FACE_COUNT; ++lf) {
        if (!portAccepts(d.ports[lf])) continue;
        Face wf = localToWorldFace(e.rotation, static_cast<Face>(lf));
        glm::ivec3 np = e.pos + faceOffsets[wf];
        BlockEntity* nb = find(np);
        if (!nb || isTransport(nb->behavior()) || !canTransfer(*nb, e)) continue;
        ItemId id = nb->offerFor(e, oppositeFace(wf), wf);
        if (id == ItemId::None) continue;
        uint16_t n = bulk ? std::min(nb->available(id), e.acceptable(id, wf)) : 1;
        if (n == 0) continue;
        intents.push_back({ np, e.pos, id, n });
        break;
    }
}

// ---- phase 3 ------------------------------------------------------------------------------

void FactorySim::apply() {
    for (const TransferIntent& in : intents) {
        BlockEntity* src = find(in.from);
        BlockEntity* dst = find(in.to);
        if (!src || !dst || !src->enabled() || !dst->enabled()) continue;
        int f = faceFromNormal(in.to - in.from);
        if (f < 0) continue;
        Face inFace = oppositeFace(static_cast<Face>(f));
        // Re-check capacity at apply time: an earlier intent this tick may have filled it.
        uint16_t n = std::min<uint16_t>(in.count, dst->acceptable(in.item, inFace));
        if (n == 0) continue;
        n = src->extract(in.item, n);
        if (n) dst->insert(in.item, n);
    }
}

// Blocks whose look depends on their contents (fluid pipes and tanks) get their construct
// re-meshed when that changes -- the owner rebuilds every construct flagged meshDirty. The
// mesher reads `visualKey`, so this is the one place that decides what a block shows.
void FactorySim::refreshVisuals() {
    for (Construct* c : targets) {
        for (BlockEntity& e : c->entities) {
            if (!e.def().variantFaces) continue;
            uint8_t v = visualVariant(e);
            // A pipe drains to empty between units; keeping its last tint until a different
            // fluid arrives avoids re-meshing (and flickering) every few ticks.
            if (v == 0 && e.behavior() == BlockBehavior::Pipe) v = e.visualKey;
            if (v == e.visualKey) continue;
            e.visualKey = v;
            c->meshDirty = true;
        }
    }
}

std::string describeEntity(const BlockEntity& e) {
    const BlockData& d = e.def();
    std::string s = d.name;
    s += " [rot " + std::to_string(e.rotation) + "]";
    if (!e.enabled()) s += " OFF";
    if (e.isPort()) {
        if (char id = e.portId()) { s += " id="; s += id; }
        if (e.portItem() != ItemId::None) s += " item=" + std::string(itemInfo(e.portItem()).name);
        s += " count=" + std::to_string(std::get<PortState>(e.ext).count);
    }

    if (auto* c = std::get_if<ConveyorState>(&e.ext)) {
        s += " belt " + std::to_string(c->count) + "/" + std::to_string(BELT_CAPACITY);
        for (int i = 0; i < c->count; ++i) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "\n  %s @%.2f",
                          std::string(itemInfo(c->at(i).item).name).c_str(), c->at(i).progress);
            s += buf;
        }
    } else if (auto* m = std::get_if<MachineState>(&e.ext)) {
        if (m->recipe >= 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), " %s %.0f%%", recipeTable[m->recipe].name, m->progress * 100.0f);
            s += buf;
        } else s += " idle";
        s += " effects=" + effectFlagsText(m->fx.flags);
        for (int i = 0; i < e.inv.slotCount; ++i)
            if (!e.inv.slots[i].empty())
                s += "\n  in  " + std::string(itemInfo(e.inv.slots[i].item).name) + " x" + std::to_string(e.inv.slots[i].count);
        for (int i = 0; i < m->out.slotCount; ++i)
            if (!m->out.slots[i].empty())
                s += "\n  out " + std::string(itemInfo(m->out.slots[i].item).name) + " x" + std::to_string(m->out.slots[i].count);
    } else if (d.slotCount > 0) {
        if (e.inv.empty()) s += " (empty)";
        for (int i = 0; i < e.inv.slotCount; ++i) {
            const ItemStack& st = e.inv.slots[i];
            if (st.empty()) continue;
            s += "\n  " + std::string(itemInfo(st.item).name) + " x" + std::to_string(st.count);
        }
    }
    return s;
}
