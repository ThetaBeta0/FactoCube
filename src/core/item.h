#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

enum class ItemType : uint8_t {
    BLOCK,
    UNREFINED,
    METAL,
    CERAMIC,
    POLYMER,
    ELECTRONIC,
    CHEMICAL,
    LIQUID,
    ORGANIC,
};

// Items move through the factory as IDs, never as Item structs. The order here MUST mirror
// itemTable (the static_assert below catches a count drift; keep the two lists in step).
enum class ItemId : uint16_t {
    Scrap, IronOre, CopperOre, TitaniumOre, Limestone, Clay, Sand, Gravel,
    IronIngot, CopperIngot, SteelIngot, AluminumIngot, TitaniumIngot, BronzeIngot,
    Glass, CeramicPlate,
    Plastic, Rubber,
    CircuitBoard, Microchip,
    SulfuricAcid, SodiumHydroxide,
    Slag, Water, Oil, Mud, MoltenMetal,
    Wood, Biofuel, Algae,

    Count,
    None = 0xFFFF
};

struct Item {
    std::string_view name;
    std::string_view description;
    ItemType type;
    unsigned int iconTileX, iconTileY; // FactoCubeItemTextures.png tile; (0,0) is the error tile
};

// Icons live in FactoCubeItemTextures.png (16x16 tiles): (0,0) error, (0,15) the slot frame,
// (1..9,15) = Iron, Aluminum, Bronze, Steel, Titanium, Copper, Limestone, Gravel, Glass,
// (0..4,13) = the fluid drops Oil, Water, Mud, Slag, Molten Metal.
// Items without art yet point at the error tile so a missing icon is obvious in-game.
inline constexpr Item itemTable[] = {
    // Unrefined
    { "Scrap", "Unrefined scrap material.", ItemType::UNREFINED, 10, 15 },
    { "Iron Ore", "Unrefined iron ore.", ItemType::UNREFINED, 1, 14 },
    { "Copper Ore", "Unrefined copper ore.", ItemType::UNREFINED, 2, 12 },
    { "Titanium Ore", "Unrefined titanium ore.", ItemType::UNREFINED, 0, 0 },
    { "Limestone", "Unrefined limestone rock.", ItemType::UNREFINED, 7, 15 },
    { "Clay", "Unrefined clay material.", ItemType::UNREFINED, 11, 15 },
    { "Sand", "Unrefined sand material.", ItemType::UNREFINED, 12, 15 },
    { "Gravel", "Crushed rock.", ItemType::UNREFINED, 8, 15 },
    // Metals
    { "Iron Ingot", "Refined iron metal.", ItemType::METAL, 1, 15 },
    { "Copper Ingot", "Refined copper metal.", ItemType::METAL, 6, 15 },
    { "Steel Ingot", "Refined steel metal.", ItemType::METAL, 4, 15 },
    { "Aluminum Ingot", "Refined aluminum metal.", ItemType::METAL, 2, 15 },
    { "Titanium Ingot", "Refined titanium metal.", ItemType::METAL, 5, 15 },
    { "Bronze Ingot", "Copper-tin alloy.", ItemType::METAL, 3, 15 },
    // Ceramics
    { "Glass", "Refined glass material.", ItemType::CERAMIC, 9, 15 },
    { "Ceramic Plate", "Refined ceramic material.", ItemType::CERAMIC, 0, 0 },
    // Polymers
    { "Plastic", "Refined plastic material.", ItemType::POLYMER, 0, 0 },
    { "Rubber", "Refined rubber material.", ItemType::POLYMER, 0, 0 },
    // Electronics
    { "Circuit Board", "Electronic circuit board.", ItemType::ELECTRONIC, 0, 0 },
    { "Microchip", "Electronic microchip.", ItemType::ELECTRONIC, 0, 0 },
    // Chemicals
    { "Sulfuric Acid", "Strong chemical acid.", ItemType::CHEMICAL, 0, 0 },
    { "Sodium Hydroxide", "Strong chemical base.", ItemType::CHEMICAL, 0, 0 },
    // Liquids (the only items pipes, tanks and pipe ports will carry)
    { "Slag", "Molten waste material.", ItemType::LIQUID, 3, 13 },
    { "Water", "Liquid water.", ItemType::LIQUID, 1, 13 },
    { "Oil", "Liquid petroleum.", ItemType::LIQUID, 0, 13 },
    { "Mud", "Wet silt and clay.", ItemType::LIQUID, 2, 13 },
    { "Molten Metal", "Metal hot enough to pour.", ItemType::LIQUID, 4, 13 },
    // Organics
    { "Wood", "Organic wood material.", ItemType::ORGANIC, 0, 0 },
    { "Biofuel", "Organic biofuel material.", ItemType::ORGANIC, 0, 0 },
    { "Algae", "Organic algae material.", ItemType::ORGANIC, 0, 0 },
};
static_assert(std::size(itemTable) == (size_t)ItemId::Count, "itemTable and ItemId drifted apart");

inline const Item& itemInfo(ItemId id) { return itemTable[(size_t)id]; }

inline bool isFluid(ItemId id) { return id != ItemId::None && itemInfo(id).type == ItemType::LIQUID; }

// Colour column a fluid selects in the block atlas' pipe (row 11) and tank (row 10) tiles:
// the plain tile sits at x = 10 and each fluid shifts it right by this much.
inline uint8_t fluidVariant(ItemId id) {
    switch (id) {
        case ItemId::Water:       return 1;
        case ItemId::Oil:         return 2;
        case ItemId::Mud:         return 3;
        case ItemId::Slag:        return 4;
        case ItemId::MoltenMetal: return 5;
        default:                  return 0;
    }
}
