#include "sim/logic.h"

#include <algorithm>

#include "sim/entity.h"
#include "sim/recipe.h"

Effect combineEffects(const Effect& a, const Effect& b) {
    Effect r;
    r.speed       = a.speed * b.speed;
    r.bonusChance = 1.0f - (1.0f - std::clamp(a.bonusChance, 0.0f, 1.0f))
                         * (1.0f - std::clamp(b.bonusChance, 0.0f, 1.0f));
    r.flags       = a.flags | b.flags;
    return r;
}

// ---- ADD ADDITIONAL BLOCKS HERE --------------------------------------------------------
// One case per Additional block type. Check `machine.type` to make an effect specific to one
// machine; leave it unchecked to affect any machine the block touches. Returning a default
// Effect grants nothing.
Effect effectOf(const BlockEntity& additional, const BlockEntity& machine) {
    Effect fx;
    switch (static_cast<BlockType>(additional.type)) {
        case MAGNET:
            // Magnetic separation for any machine; a Sifter also runs a little faster.
            fx.flags |= EFFECT_MAGNETIC;
            if (machine.type == SIFTER) fx.speed = 1.25f;
            break;

        // Example of a second block:
        // case HEATER:
        //     fx.flags |= EFFECT_HEATED;
        //     fx.bonusChance = 0.10f;      // 10% chance of an extra output per craft
        //     break;

        default:
            break;
    }
    return fx;
}

// ---- ADD MACHINE HOOKS HERE ------------------------------------------------------------
// Called once per finished craft, after the recipe's outputs (and any bonus) are in the
// machine's output slots. `machine.inv` is the input side, MachineState::out the output side.
void onCraftComplete(BlockEntity& machine, const Recipe& recipe, const Effect& fx) {
    (void)recipe; (void)fx;
    switch (static_cast<BlockType>(machine.type)) {
        // case GRINDER: {
        //     // e.g. every 10th craft also yields a Scrap:
        //     auto& ms = std::get<MachineState>(machine.ext);
        //     if (ms.crafted % 10 == 0) ms.out.insert(ItemId::Scrap, 1);
        //     break;
        // }
        default:
            break;
    }
}

std::string effectFlagsText(uint32_t flags) {
    static const struct { uint32_t bit; const char* name; } names[] = {
        { EFFECT_MAGNETIC,    "Magnetic" },
        { EFFECT_HEATED,      "Heated" },
        { EFFECT_PRESSURIZED, "Pressurized" },
    };
    std::string s;
    for (const auto& n : names) {
        if (!(flags & n.bit)) continue;
        if (!s.empty()) s += ", ";
        s += n.name;
    }
    return s.empty() ? "none" : s;
}
