#pragma once

#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------------------
// Custom block logic lives here and in logic.cpp. Two hooks cover most machines:
//
//   effectOf(additional, machine)  what an Additional block (magnet, heater...) grants a
//                                  Machine it touches Effect-port-to-Effect-port. Returns an
//                                  Effect: a speed multiplier, a bonus-output chance and a
//                                  set of flags. Recipes can `require` flags, so an effect can
//                                  unlock a recipe as well as tune one.
//
//   onCraftComplete(machine, ...)  runs after a recipe's outputs are produced, for machines
//                                  that need more than "inputs -> outputs" (side products,
//                                  wear, counters...).
//
// Both are plain switches on the block type: add a `case` and you're done. docs/BLOCKS.md has
// worked examples.
// ---------------------------------------------------------------------------------------

struct BlockEntity;
struct Recipe;

// Named capabilities an Additional block can grant. Recipes list the ones they need.
enum EffectFlag : uint32_t {
    EFFECT_MAGNETIC    = 1u << 0,   // Magnet: separates ferrous material (Sifter)
    EFFECT_HEATED      = 1u << 1,   // reserved: a heater next to a furnace / dryer
    EFFECT_PRESSURIZED = 1u << 2,   // reserved: a compressor next to a press
};

struct Effect {
    float    speed       = 1.0f;   // multiplies crafting speed
    float    bonusChance = 0.0f;   // chance (0..1) of one extra of every output per craft
    uint32_t flags       = 0;      // EffectFlag bits granted

    bool has(uint32_t required) const { return (flags & required) == required; }
};

// Speeds multiply, bonus chances stack as independent rolls, flags union.
Effect combineEffects(const Effect& a, const Effect& b);

// The Effect one Additional block grants one Machine (they touch through Effect ports).
Effect effectOf(const BlockEntity& additional, const BlockEntity& machine);

// Machine-specific work after a craft's outputs were added (default: nothing).
void onCraftComplete(BlockEntity& machine, const Recipe& recipe, const Effect& fx);

// "Magnetic, Heated" for the menu / debug readout; "none" when empty.
std::string effectFlagsText(uint32_t flags);
