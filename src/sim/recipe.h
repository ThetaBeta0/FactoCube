#pragma once

#include <cstdint>
#include <iterator>
#include <string>

#include "core/block.h"
#include "core/item.h"
#include "sim/logic.h"

// ---------------------------------------------------------------------------------------
// Recipes: what a Machine turns into what, through which of its faces, and how long it takes.
//
//   { GRINDER, "Crush limestone", 2.0f, 0,
//     { { Bottom, ItemId::Limestone, 1 } },          // inputs:  face, item, count
//     { { Top,    ItemId::Gravel,    2 } } },        // outputs: face, item, count
//
// Faces are the machine's LOCAL faces (rotation is applied for you) and must carry an Input
// (for inputs) / Output (for outputs) port in blockTable; ANY_FACE means "whichever port of
// that direction". The fourth field lists EffectFlags the machine must currently be granted
// (see sim/logic.h) -- put such recipes BEFORE the plain ones, the first runnable recipe wins.
//
// The ratio is simply the counts: 2 Gravel -> 1 Sand + 1 Iron Ore. A machine's crafting speed
// multiplier (BlockData::itemsPerSecond) divides `seconds`, and Additional blocks can multiply
// it further.
// ---------------------------------------------------------------------------------------

constexpr int     RECIPE_MAX_IO = 4;
constexpr uint8_t ANY_FACE      = 0xFF;

struct RecipeIO {
    uint8_t  face;    // local Face, or ANY_FACE
    ItemId   item;
    uint16_t count;   // 0 marks the end of the list
};

struct Recipe {
    BlockType   machine;
    const char* name;
    float       seconds;                   // base craft time
    uint32_t    needs;                     // EffectFlags that must all be present (0 = none)
    RecipeIO    inputs[RECIPE_MAX_IO];
    RecipeIO    outputs[RECIPE_MAX_IO];
};

// ---- ADD RECIPES HERE ------------------------------------------------------------------
inline const Recipe recipeTable[] = {
    // Sifter: a magnet pulls the iron out of the heavy fraction; without one it's just scrap.
    { SIFTER,  "Magnetic sift",   3.0f, EFFECT_MAGNETIC,
      { { Bottom, ItemId::Gravel, 2 } },
      { { Front, ItemId::Sand, 1 }, { Top, ItemId::IronOre, 1 } } },
    { SIFTER,  "Sift gravel",     3.0f, 0,
      { { Bottom, ItemId::Gravel, 2 } },
      { { Front, ItemId::Sand, 1 }, { Top, ItemId::Scrap, 1 } } },

    // Grinder: rock down to gravel, gravel down to sand.
    { GRINDER, "Crush limestone", 2.0f, 0,
      { { Bottom, ItemId::Limestone, 1 } },
      { { Top, ItemId::Gravel, 2 } } },
    { GRINDER, "Grind gravel",    2.0f, 0,
      { { Bottom, ItemId::Gravel, 2 } },
      { { Top, ItemId::Sand, 3 } } },
};
inline constexpr int RECIPE_COUNT = (int)std::size(recipeTable);

inline bool ioEnd(const RecipeIO& io) { return io.count == 0; }
inline bool ioOnFace(const RecipeIO& io, int localFace) {
    return localFace < 0 || io.face == ANY_FACE || io.face == localFace;
}

// Does any recipe of this machine take `id` through `localFace` (-1 = through any face)?
inline bool recipeTakesOn(uint8_t machine, ItemId id, int localFace) {
    for (const Recipe& r : recipeTable) {
        if (r.machine != machine) continue;
        for (const RecipeIO& io : r.inputs) {
            if (ioEnd(io)) break;
            if (io.item == id && ioOnFace(io, localFace)) return true;
        }
    }
    return false;
}

// Does any recipe of this machine give out `id` through `localFace` (-1 = through any face)?
inline bool recipeOffersOn(uint8_t machine, ItemId id, int localFace) {
    for (const Recipe& r : recipeTable) {
        if (r.machine != machine) continue;
        for (const RecipeIO& io : r.outputs) {
            if (ioEnd(io)) break;
            if (io.item == id && ioOnFace(io, localFace)) return true;
        }
    }
    return false;
}

// Most inputs / outputs any one recipe of this machine has: the size of its slot rows.
inline int recipeMaxIO(uint8_t machine, bool outputs) {
    int best = 0;
    for (const Recipe& r : recipeTable) {
        if (r.machine != machine) continue;
        int n = 0;
        for (const RecipeIO& io : (outputs ? r.outputs : r.inputs)) { if (ioEnd(io)) break; ++n; }
        best = std::max(best, n);
    }
    return best;
}

// Checks every recipe against blockTable: machine behaviour, port directions, item ids.
// Returns an empty string when all is well, else one line per problem (log it at startup).
inline std::string validateRecipes() {
    std::string out;
    auto bad = [&](const Recipe& r, const std::string& what) {
        out += std::string("recipe '") + r.name + "' (" + blockTable[r.machine].name + "): " + what + "\n";
    };
    for (const Recipe& r : recipeTable) {
        const BlockData& d = blockTable[r.machine];
        if (d.behavior != BlockBehavior::Machine) bad(r, "block is not a Machine");
        if (r.seconds <= 0.0f) bad(r, "seconds must be > 0");
        bool anyIn = false, anyOut = false;
        for (const RecipeIO& io : r.inputs) {
            if (ioEnd(io)) break;
            anyIn = true;
            if (io.item == ItemId::None || (size_t)io.item >= (size_t)ItemId::Count) bad(r, "input has no item");
            if (io.face != ANY_FACE && (io.face >= FACE_COUNT || !portAccepts(d.ports[io.face])))
                bad(r, "input face has no Input port");
        }
        for (const RecipeIO& io : r.outputs) {
            if (ioEnd(io)) break;
            anyOut = true;
            if (io.item == ItemId::None || (size_t)io.item >= (size_t)ItemId::Count) bad(r, "output has no item");
            if (io.face != ANY_FACE && (io.face >= FACE_COUNT || !portEmits(d.ports[io.face])))
                bad(r, "output face has no Output port");
        }
        if (!anyIn)  bad(r, "no inputs");
        if (!anyOut) bad(r, "no outputs");
    }
    return out;
}
