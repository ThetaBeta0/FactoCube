#pragma once

#include <algorithm>
#include <cstdint>

#include "core/item.h"

constexpr int INVENTORY_MAX_SLOTS = 8;

struct ItemStack {
    ItemId   item  = ItemId::None;
    uint16_t count = 0;

    bool empty() const { return count == 0 || item == ItemId::None; }
};

// Fixed-size slot inventory. `slotCount` / `slotCap` come from the block's BlockData; the
// unused tail of `slots` is never touched, so this stays trivially copyable and serialisable.
struct Inventory {
    ItemStack slots[INVENTORY_MAX_SLOTS];
    uint8_t   slotCount = 0;
    uint16_t  slotCap   = 0;

    void configure(uint8_t slots_, uint16_t cap) {
        slotCount = (uint8_t)std::min<int>(slots_, INVENTORY_MAX_SLOTS);
        slotCap   = cap;
        clear();
    }

    void clear() { for (auto& s : slots) s = ItemStack{}; }

    // How many more of `id` could be inserted right now.
    uint16_t acceptable(ItemId id) const {
        if (id == ItemId::None) return 0;
        uint32_t room = 0;
        for (int i = 0; i < slotCount; ++i) {
            if (slots[i].empty())           room += slotCap;
            else if (slots[i].item == id)   room += slotCap - std::min(slotCap, slots[i].count);
        }
        return (uint16_t)std::min<uint32_t>(room, 0xFFFF);
    }

    // Tops up matching stacks first, then opens empty slots. Returns the amount inserted.
    uint16_t insert(ItemId id, uint16_t n) {
        if (id == ItemId::None) return 0;
        uint16_t left = n;
        for (int i = 0; i < slotCount && left; ++i) {
            if (slots[i].empty() || slots[i].item != id) continue;
            uint16_t take = (uint16_t)std::min<int>(left, slotCap - slots[i].count);
            slots[i].count += take;
            left -= take;
        }
        for (int i = 0; i < slotCount && left; ++i) {
            if (!slots[i].empty()) continue;
            uint16_t take = std::min(left, slotCap);
            slots[i] = { id, take };
            left -= take;
        }
        return n - left;
    }

    // Returns the amount actually removed (may be less than asked for).
    uint16_t extract(ItemId id, uint16_t n) {
        if (id == ItemId::None) return 0;
        uint16_t left = n;
        for (int i = 0; i < slotCount && left; ++i) {
            if (slots[i].empty() || slots[i].item != id) continue;
            uint16_t take = std::min(left, slots[i].count);
            slots[i].count -= take;
            if (slots[i].count == 0) slots[i] = ItemStack{};
            left -= take;
        }
        return n - left;
    }

    uint16_t count(ItemId id) const {
        uint32_t c = 0;
        for (int i = 0; i < slotCount; ++i)
            if (!slots[i].empty() && slots[i].item == id) c += slots[i].count;
        return (uint16_t)std::min<uint32_t>(c, 0xFFFF);
    }

    uint16_t total() const {
        uint32_t c = 0;
        for (int i = 0; i < slotCount; ++i)
            if (!slots[i].empty()) c += slots[i].count;
        return (uint16_t)std::min<uint32_t>(c, 0xFFFF);
    }

    bool empty() const { return total() == 0; }

    // The first item held, or None -- what a transport block will try to pull out.
    ItemId firstItem() const {
        for (int i = 0; i < slotCount; ++i)
            if (!slots[i].empty()) return slots[i].item;
        return ItemId::None;
    }
};
