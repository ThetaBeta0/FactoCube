#include "ui/block_menu.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/constants.h"
#include "render/text.h"
#include "render/ui.h"
#include "sim/factory.h"
#include "sim/logic.h"
#include "sim/recipe.h"
#include "ui/alert.h"
#include "ui/widgets.h"

using namespace wg;   // Rect, Ctx, slot(), panel(), text helpers, tile/colour constants

namespace {

// One row of slots in the panel: a caption, then either an inventory (count under each slot)
// or a belt (travel % under each occupied position).
struct SlotRow {
    const char*      caption;
    const Inventory* inv;    // one of these two is set
    const ConveyorState* belt;
    int count() const { return belt ? BELT_CAPACITY : inv->slotCount; }
};

// ---- The block panel -------------------------------------------------------------------
void drawPanel(BlockMenu& m, Ctx& c, BlockEntity& e, double now) {
    const BlockData& d = e.def();
    const ConveyorState* belt = std::get_if<ConveyorState>(&e.ext);
    const PortState*     port = std::get_if<PortState>(&e.ext);
    const MachineState*  mach = std::get_if<MachineState>(&e.ext);
    const bool isPort = e.isPort();

    // Slot rows: a machine shows its input and output sides, everything else one row.
    std::vector<SlotRow> rows;
    if (mach) {
        rows.push_back({ "Input",  &e.inv,    nullptr });
        rows.push_back({ "Output", &mach->out, nullptr });
    } else if (belt) {
        rows.push_back({ nullptr, nullptr, belt });
    } else if (e.inv.slotCount) {
        rows.push_back({ nullptr, &e.inv, nullptr });
    }
    int maxCols = 1;
    for (const SlotRow& r : rows) maxCols = std::max(maxCols, std::min(r.count(), MENU_SLOT_COLS));
    auto rowHeight = [&](const SlotRow& r) {
        int cols = std::clamp(r.count(), 1, MENU_SLOT_COLS);
        int n = (r.count() + cols - 1) / cols;
        return (r.caption ? c.bodyH : 0.0f) + n * (c.S + c.bodyH + c.pad);
    };

    char sub[128];
    if (belt)
        std::snprintf(sub, sizeof(sub), "Belt %d/%d", belt->count, BELT_CAPACITY);
    else if (port)
        std::snprintf(sub, sizeof(sub), "%s |  %s %u",
                      e.behavior() == BlockBehavior::PortIn ? "Source" : "Sink",
                      e.behavior() == BlockBehavior::PortIn ? "spawned" : "received", (unsigned)port->count);
    else if (mach) {
        if (mach->recipe >= 0)
            std::snprintf(sub, sizeof(sub), "%s  %d%%  |  crafted %u", recipeTable[mach->recipe].name,
                          (int)std::lround(mach->progress * 100.0f), (unsigned)mach->crafted);
        else
            std::snprintf(sub, sizeof(sub), "Idle  |  crafted %u", (unsigned)mach->crafted);
    }
    else if (e.inv.slotCount)
        std::snprintf(sub, sizeof(sub), "%d slot%s x %d",
                      e.inv.slotCount, e.inv.slotCount == 1 ? "" : "s", e.inv.slotCap);
    else
        std::snprintf(sub, sizeof(sub), "No inventory");

    // Machines also list what touches them and their recipes' shape.
    std::string effects;
    if (mach) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Effects: %s  |  speed x%.2f  bonus %d%%",
                      effectFlagsText(mach->fx.flags).c_str(), mach->fx.speed,
                      (int)std::lround(mach->fx.bonusChance * 100.0f));
        effects = buf;
    }

    // Legend lines name what each occupied slot holds.
    std::vector<std::string> legend;
    for (const SlotRow& r : rows) {
        for (int i = 0; i < r.count(); ++i) {
            char entry[96];
            if (r.belt) {
                if (i >= r.belt->count) continue;
                std::snprintf(entry, sizeof(entry), "%d. %s", i + 1,
                              std::string(itemInfo(r.belt->at(i).item).name).c_str());
            } else {
                const ItemStack& st = r.inv->slots[i];
                if (st.empty()) continue;
                std::snprintf(entry, sizeof(entry), "%s%d. %s x%u", r.caption ? (r.caption[0] == 'I' ? "in " : "out ") : "",
                              i + 1, std::string(itemInfo(st.item).name).c_str(), (unsigned)st.count);
            }
            legend.emplace_back(entry);
        }
    }

    // Settings row: Item and ID for ports, the ON/OFF switch for everything.
    const int nBoxes = isPort ? 3 : 1;
    const float settingsW = nBoxes * c.S + (nBoxes + 1) * c.pad;
    const float settingsH = c.S + c.bodyH + c.pad;

    std::string hint;
    if (m.capturing) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "Press a letter for the ID  (Backspace clears)   %.0fs",
                      std::ceil(MENU_ID_CAPTURE_SECONDS - (now - m.captureStart)));
        hint = buf;
    }

    float w = std::max({ maxCols * c.S + (maxCols + 1) * c.pad, settingsW, 320.0f * c.u });
    w = std::max(w, c.text.textWidth(d.name, c.titleScale) + 2.0f * c.pad);
    w = std::max(w, c.text.textWidth(sub, c.bodyScale) + 2.0f * c.pad);
    if (!effects.empty()) w = std::max(w, c.text.textWidth(effects, c.bodyScale) + 2.0f * c.pad);
    for (const auto& line : legend) w = std::max(w, c.text.textWidth(line, c.bodyScale) + 2.0f * c.pad);
    if (!hint.empty()) w = std::max(w, c.text.textWidth(hint, c.bodyScale) + 2.0f * c.pad);
    float h = c.pad + c.titleH + c.bodyH + c.pad;
    for (const SlotRow& r : rows) h += rowHeight(r);
    if (mach) h += 10.0f * c.u + c.pad + c.bodyH + c.pad;   // progress bar + effects line
    h += settingsH
       + (legend.empty() ? 0.0f : legend.size() * c.bodyH + c.pad)
       + (hint.empty() ? 0.0f : c.bodyH + c.pad);
    Rect p = panel(c, w, h);

    // Header.
    float ty = p.y + c.pad;
    shadowText(c, d.name, p.x + c.pad, ty, c.titleScale);
    ty += c.titleH;
    shadowText(c, sub, p.x + c.pad, ty, c.bodyScale, kDim);
    ty += c.bodyH + c.pad;

    // Slot rows (view only).
    for (const SlotRow& r : rows) {
        if (r.caption) { shadowText(c, r.caption, p.x + c.pad, ty, c.bodyScale, kFaint); ty += c.bodyH; }
        const int cols = std::clamp(r.count(), 1, MENU_SLOT_COLS);
        for (int i = 0; i < r.count(); ++i) {
            Rect sr{ p.x + c.pad + (i % cols) * (c.S + c.pad), ty + (i / cols) * (c.S + c.bodyH + c.pad), c.S, c.S };
            ItemId id = ItemId::None;
            char label[16] = "";
            if (r.belt) {
                if (i < r.belt->count) {
                    id = r.belt->at(i).item;
                    std::snprintf(label, sizeof(label), "%d%%", (int)std::lround(r.belt->at(i).progress * 100.0f));
                }
            } else if (!r.inv->slots[i].empty()) {
                id = r.inv->slots[i].item;
                std::snprintf(label, sizeof(label), "%u", (unsigned)r.inv->slots[i].count);
            }
            slot(c, sr, id == ItemId::None ? kNoTile : iconOf(id), false);
            if (label[0]) {   // count / progress, right-aligned under the slot
                float lw = c.text.textWidth(label, c.bodyScale);
                shadowText(c, label, sr.x + sr.w - lw, sr.y + sr.h + 2.0f * c.u, c.bodyScale);
            }
        }
        ty += rowHeight(r) - (r.caption ? c.bodyH : 0.0f);
    }

    // Machine: progress bar and the effects line.
    if (mach) {
        float bw = w - 2.0f * c.pad, bh = 10.0f * c.u;
        c.ui.rect(p.x + c.pad, ty, bw, bh, MENU_BORDER_COLOR);
        if (mach->recipe >= 0)
            c.ui.rect(p.x + c.pad + 2.0f * c.u, ty + 2.0f * c.u, (bw - 4.0f * c.u) * mach->progress, bh - 4.0f * c.u,
                      glm::vec4(kGreen, 1.0f));
        ty += bh + c.pad;
        shadowText(c, effects, p.x + c.pad, ty, c.bodyScale, kDim);
        ty += c.bodyH + c.pad;
    }

    // Settings row.
    float bx = p.x + c.pad;
    if (isPort) {
        // Item: + until one is chosen; clicking opens the full list.
        Rect itemBox{ bx, ty, c.S, c.S };
        ItemId cur = e.portItem();
        if (slot(c, itemBox, cur == ItemId::None ? kPlusTile : iconOf(cur), true)) {
            m.pickerOpen = true;
            m.capturing = false;
        }
        shadowText(c, "Item", bx, ty + c.S + 2.0f * c.u, c.bodyScale, kDim);
        bx += c.S + c.pad;

        // ID: + until a letter is chosen; clicking starts the timed capture (countdown shown).
        Rect idBox{ bx, ty, c.S, c.S };
        char id = e.portId();
        bool clicked = slot(c, idBox, (m.capturing || id) ? kNoTile : kPlusTile, true);
        if (m.capturing) {
            int left = (int)std::ceil(MENU_ID_CAPTURE_SECONDS - (now - m.captureStart));
            centredText(c, std::to_string(std::max(left, 1)), idBox, c.titleScale, kYellow);
        } else if (id) {
            centredText(c, std::string(1, id), idBox, c.titleScale * 1.4f, kWhite);
        }
        if (clicked && !m.capturing) {
            m.capturing = true;
            m.captureStart = now;
            m.pickerOpen = false;
        }
        shadowText(c, "ID", bx, ty + c.S + 2.0f * c.u, c.bodyScale, kDim);
        bx += c.S + c.pad;
    }
    // Enabled: the switch programming will drive later.
    Rect enBox{ bx, ty, c.S, c.S };
    if (slot(c, enBox, kNoTile, true)) e.setEnabled(!e.enabled());
    bool on = e.enabled();
    centredText(c, on ? "ON" : "OFF", enBox, c.titleScale, on ? kGreen : kRed);
    shadowText(c, "Enabled", bx, ty + c.S + 2.0f * c.u, c.bodyScale, kDim);
    ty += settingsH;

    // Legend.
    for (size_t i = 0; i < legend.size(); ++i)
        shadowText(c, legend[i], p.x + c.pad, ty + i * c.bodyH, c.bodyScale, glm::vec3(0.9f));
    if (!legend.empty()) ty += legend.size() * c.bodyH + c.pad;

    if (!hint.empty()) shadowText(c, hint, p.x + c.pad, ty, c.bodyScale, kYellow);
}

// ---- The item picker ---------------------------------------------------------------------
// "None" (the minus) first, then every item in ItemId order -- fluids only for a fluid block.
// A click sets the port's item.
void drawPicker(BlockMenu& m, Ctx& c, BlockEntity& e) {
    std::vector<ItemId> choices;
    choices.push_back(ItemId::None);
    for (int i = 0; i < (int)ItemId::Count; ++i) {
        ItemId id = (ItemId)i;
        if (!e.def().fluid || isFluid(id)) choices.push_back(id);
    }

    const int n    = (int)choices.size();
    const int cols = MENU_PICKER_COLS;
    const int rows = (n + cols - 1) / cols;
    const char* title = e.def().fluid ? "Select fluid   (Esc: back)" : "Select item   (Esc: back)";

    float w = std::max(cols * c.S + (cols + 1) * c.pad, c.text.textWidth(title, c.titleScale) + 2.0f * c.pad);
    float h = c.pad + c.titleH + c.pad + rows * (c.S + c.pad) + c.bodyH + c.pad;
    Rect p = panel(c, w, h);

    shadowText(c, title, p.x + c.pad, p.y + c.pad, c.titleScale);
    const float gy = p.y + c.pad + c.titleH + c.pad;

    std::string hovered;
    for (int i = 0; i < n; ++i) {
        Rect r{ p.x + c.pad + (i % cols) * (c.S + c.pad), gy + (i / cols) * (c.S + c.pad), c.S, c.S };
        ItemId id = choices[i];
        if (r.contains(c.ptr.pos)) hovered = (id == ItemId::None) ? "None" : std::string(itemInfo(id).name);
        if (slot(c, r, (id == ItemId::None) ? kMinusTile : iconOf(id), true)) {
            e.setPortItem(id);
            if (e.behavior() == BlockBehavior::PortIn) e.inv.clear();   // don't keep feeding the old item
            m.pickerOpen = false;
        }
    }
    shadowText(c, hovered.empty() ? "Click an item; - clears the choice" : hovered,
               p.x + c.pad, gy + rows * (c.S + c.pad), c.bodyScale, hovered.empty() ? kFaint : kWhite);
}

} // namespace

void BlockMenu::frame(UIRenderer& ui, TextRenderer& text, BlockEntity& e, FactorySim& sim,
                      AlertSystem& alerts, unsigned int itemTexture, int fbW, int fbH,
                      const MenuPointer& ptr, const MenuKeys& keys, double now) {
    // Letter capture resolves before anything is drawn: a key, Backspace, or the timeout.
    if (capturing) {
        if (now - captureStart > MENU_ID_CAPTURE_SECONDS) {
            capturing = false;
            alerts.show("ID not set (timed out)", 1.5f, 5.0f, kRed);
        } else if (keys.backspace) {
            e.setPortId(0);
            capturing = false;
        } else if (keys.letter) {
            // Upper case names a Port In, lower case a Port Out; each letter is unique per rig.
            char id = (e.behavior() == BlockBehavior::PortIn) ? keys.letter
                                                              : (char)std::tolower((unsigned char)keys.letter);
            BlockEntity* other = sim.findPortById(id);
            if (other && other != &e) {
                alerts.show(std::string("ID '") + id + "' is already used", 1.5f, 5.0f, kRed);
            } else {
                e.setPortId(id);
            }
            capturing = false;
        }
    }

    Ctx c = makeCtx(ui, text, itemTexture, fbW, fbH, ptr);

    ui.begin(fbW, fbH);
    if (pickerOpen) drawPicker(*this, c, e);
    else            drawPanel(*this, c, e, now);
    cursor(c);
    ui.end();
}
