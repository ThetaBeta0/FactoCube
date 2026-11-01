// Headless checks for the block/rotation/transfer layer. Build + run with `make test`.
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "construct/construct.h"
#include "core/block.h"
#include "core/rotation.h"
#include "sim/factory.h"
#include "sim/logic.h"
#include "sim/recipe.h"

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void section(const char* name) { std::printf("[%s]\n", name); }

// Run the sim for `seconds` of simulated time in exact FACTORY_TICK_DT steps.
static void runFor(FactorySim& sim, float seconds) {
    int steps = (int)std::lround(seconds / FACTORY_TICK_DT);
    for (int i = 0; i < steps; ++i) sim.tick(FACTORY_TICK_DT);
}

static int totalItems(const std::vector<Construct*>& cs) {
    int n = 0;
    for (auto* c : cs) for (auto& e : c->entities) n += e.itemCount();
    return n;
}

// The rotation that points a block's local `local` face at world `world`.
static int rotationWith(Face local, Face world) {
    for (int r = 0; r < ROTATION_COUNT; ++r)
        if (localToWorldFace(r, local) == world) return r;
    return -1;
}

// ---------------------------------------------------------------------------------------
static void testRotationSet() {
    section("rotation set");
    const glm::ivec3 facingDir[6] = { {0,1,0}, {0,-1,0}, {1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1} };
    std::set<std::string> seen;
    for (int r = 0; r < ROTATION_COUNT; ++r) {
        const glm::imat3x3& m = kRot.mat[r];
        // proper rotation: integer determinant +1
        int det = m[0][0]*(m[1][1]*m[2][2]-m[2][1]*m[1][2])
                - m[1][0]*(m[0][1]*m[2][2]-m[2][1]*m[0][2])
                + m[2][0]*(m[0][1]*m[1][2]-m[1][1]*m[0][2]);
        CHECK(det == 1);
        // matches the float matrix the renderer uses
        for (int c = 0; c < 3; ++c) for (int row = 0; row < 3; ++row)
            CHECK(std::abs((float)m[c][row] - blockRotations[r][c][row]) < 1e-4f);
        // faceMap / faceMapInv agree with the matrix and are inverses
        for (int f = 0; f < FACE_COUNT; ++f) {
            CHECK(faceOffsets[kRot.faceMap[r][f]] == m * faceOffsets[f]);
            CHECK(kRot.faceMapInv[r][kRot.faceMap[r][f]] == f);
            CHECK(oppositeFace(localToWorldFace(r, (Face)f)) ==
                  localToWorldFace(r, oppositeFace((Face)f)));
        }
        // all 24 distinct
        std::string key;
        for (int f = 0; f < FACE_COUNT; ++f) key += (char)('0' + kRot.faceMap[r][f]);
        CHECK(seen.insert(key).second);
        // documented layout: index = facing*4 + spin, facing = where local +Y lands
        CHECK(blockForward(r) == facingDir[r / 4]);
    }
    CHECK(kRot.faceMap[0][Top] == Top && kRot.faceMap[0][Front] == Front);   // identity
}

static void testPorts() {
    section("ports through rotation");
    Block conv0 = makeBlock(CONVEYER_BELT, 0);
    CHECK(portAt(conv0, Top) == PortMode::Output);
    CHECK(portAt(conv0, Bottom) == PortMode::Input);
    CHECK(portAt(conv0, Left) == PortMode::Input);   // side-loading

    for (int r = 0; r < ROTATION_COUNT; ++r) {
        Block conv = makeBlock(CONVEYER_BELT, r);
        Face fwd = (Face)faceFromNormal(blockForward(r));
        CHECK(portAt(conv, fwd) == PortMode::Output);
        CHECK(portAt(conv, oppositeFace(fwd)) == PortMode::Input);
        Block split = makeBlock(CONVEYER_SPLITTER, r);
        CHECK(portAt(split, fwd) == PortMode::Output);
        CHECK(portAt(split, oppositeFace(fwd)) == PortMode::Input);
        int outputs = 0;
        for (int f = 0; f < FACE_COUNT; ++f) outputs += portEmits(portAt(split, (Face)f));
        CHECK(outputs == 3);
    }
    CHECK(makeBlock(CONVEYER_BELT, 99).rotation == 0);      // validated on the way in
    CHECK(makeBlock(BLOCK_TYPE_COUNT + 5, 0).type == AIR);
    CHECK(!blockTable[STONE].hasEntity() && blockTable[BARREL].hasEntity());
    CHECK(blockTable[PIPE_PLUMBING].fluid && blockTable[PIPE_TANK].fluid && !blockTable[BARREL].fluid);
}

static void testCanTransfer() {
    section("canTransfer");
    for (int r = 0; r < ROTATION_COUNT; ++r) {
        Block conv = makeBlock(CONVEYER_BELT, r);
        Block barrel = makeBlock(BARREL, 0);
        glm::ivec3 o(0), fwd = blockForward(r);
        CHECK( canTransfer(conv, o, barrel, o + fwd));      // belt pushes into the barrel ahead
        CHECK( canTransfer(barrel, o - fwd, conv, o));      // barrel behind can feed the belt
        CHECK(!canTransfer(barrel, o + fwd, conv, o));      // ...but not from in front
        CHECK(!canTransfer(conv, o, barrel, o + fwd * 2));  // not adjacent
        // a belt behind us with the same heading feeds straight in
        Block back = makeBlock(CONVEYER_BELT, r);
        CHECK( canTransfer(back, o - fwd, conv, o));
    }
    // Head-on: A points +Y at B, B points -Y at A (rotation 4 = facing -Y, spin 0).
    Block up = makeBlock(CONVEYER_BELT, 0), down = makeBlock(CONVEYER_BELT, 4);
    CHECK(blockForward(4) == glm::ivec3(0, -1, 0));
    CHECK(!canTransfer(up, {0,0,0}, down, {0,1,0}));
    CHECK(!canTransfer(down, {0,1,0}, up, {0,0,0}));
    // Side-load: A points +X into the side of B (which points +Y).
    Block east = makeBlock(CONVEYER_BELT, 8);
    CHECK(blockForward(8) == glm::ivec3(1, 0, 0));
    CHECK( canTransfer(east, {0,0,0}, up, {1,0,0}));
    CHECK(!canTransfer(up, {1,0,0}, east, {0,0,0}));
    // Terrain has no ports at all; Effect ports move nothing.
    CHECK(!canTransfer(makeBlock(STONE, 0), {0,0,0}, makeBlock(BARREL, 0), {0,1,0}));
    CHECK(!canTransfer(makeBlock(MAGNET, 0), {0,1,0}, makeBlock(BARREL, 0), {0,0,0}));
}

static void testInventory() {
    section("inventory");
    Inventory inv; inv.configure(2, 10);
    CHECK(inv.acceptable(ItemId::IronOre) == 20);
    CHECK(inv.insert(ItemId::IronOre, 15) == 15);
    CHECK(inv.count(ItemId::IronOre) == 15 && inv.total() == 15);
    CHECK(inv.acceptable(ItemId::IronOre) == 5);
    CHECK(inv.acceptable(ItemId::Scrap) == 0);            // no free slot, wrong type
    CHECK(inv.insert(ItemId::IronOre, 9) == 5);           // partial
    CHECK(inv.extract(ItemId::IronOre, 12) == 12);
    CHECK(inv.total() == 8);
    CHECK(inv.acceptable(ItemId::Scrap) == 10);           // a slot freed up
    CHECK(inv.extract(ItemId::Scrap, 1) == 0);
    CHECK(inv.extract(ItemId::IronOre, 100) == 8 && inv.empty());
    CHECK(inv.firstItem() == ItemId::None);
    CHECK(inv.insert(ItemId::None, 5) == 0);
}

static void testEntityStore() {
    section("construct entity store");
    Construct c;
    c.setBlock({0,0,0}, makeBlock(BARREL, 0));
    c.setBlock({1,0,0}, makeBlock(STONE, 0));            // no entity for terrain-ish blocks
    c.setBlock({2,0,0}, makeBlock(CONVEYER_BELT, 3));
    c.setBlock({3,0,0}, makeBlock(CONVEYER_SPLITTER, 0));
    CHECK(c.entities.size() == 3);
    CHECK(c.entityAt({1,0,0}) == nullptr);
    CHECK(c.entityAt({2,0,0}) && c.entityAt({2,0,0})->rotation == 3);
    CHECK(std::holds_alternative<ConveyorState>(c.entityAt({2,0,0})->ext));
    CHECK(std::holds_alternative<SplitterState>(c.entityAt({3,0,0})->ext));
    CHECK(c.entityAt({0,0,0})->inv.slotCount == 1 && c.entityAt({0,0,0})->inv.slotCap == 64);

    // Growing the grid (negative direction, so it shifts) keeps entity lookups intact.
    c.setBlock({-5,-5,-5}, makeBlock(BARREL, 0));
    CHECK(c.origin == glm::ivec3(-5));
    CHECK(c.entityAt({2,0,0}) && c.entityAt({2,0,0})->pos == glm::ivec3(2,0,0));
    CHECK(c.blockAt({2,0,0}) && c.blockAt({2,0,0})->type == CONVEYER_BELT);
    CHECK(c.blockAt({50,0,0}) == nullptr);

    // Removing the middle one swap-and-pops; the moved entity must still be found.
    c.setBlock({2,0,0}, Block{});
    CHECK(c.entities.size() == 3);
    CHECK(c.entityAt({2,0,0}) == nullptr);
    for (auto& [pos, idx] : c.entityIndex) CHECK(c.entities[idx].pos == pos);
    // Replacing a machine with another resets its state.
    c.entityAt({0,0,0})->inv.insert(ItemId::Scrap, 3);
    c.setBlock({0,0,0}, makeBlock(BARREL, 0));
    CHECK(c.entityAt({0,0,0})->inv.empty());
}

// Lay a barrel -> N belts -> barrel line along rotation r's forward axis, starting at `start`.
static void buildLine(Construct& c, glm::ivec3 start, int r, int belts) {
    glm::ivec3 fwd = blockForward(r);
    c.setBlock(start, makeBlock(BARREL, 0));
    for (int i = 1; i <= belts; ++i) c.setBlock(start + fwd * i, makeBlock(CONVEYER_BELT, r));
    c.setBlock(start + fwd * (belts + 1), makeBlock(BARREL, 0));
}

static void testLine(int r) {
    Construct c;
    glm::ivec3 start(3, -2, 7), fwd = blockForward(r);
    buildLine(c, start, r, 3);
    BlockEntity* src = c.entityAt(start);
    BlockEntity* dst = c.entityAt(start + fwd * 4);
    src->inv.insert(ItemId::IronOre, 10);

    FactorySim sim; sim.targets = { &c };
    // Belt travel is itemsPerSecond * BELT_SPACING = 2 cells/s, so the first item needs 1.5 s
    // for 3 cells of belt and can't have arrived before that.
    runFor(sim, 1.4f);
    CHECK(dst->inv.total() == 0);
    CHECK(totalItems(sim.targets) == 10);
    // ...all 10 need 1.5 s + 9 more spaced at the belt's 8/s = ~2.6 s; allow slack.
    runFor(sim, 1.8f);
    CHECK(dst->inv.total() == 10);
    CHECK(src->inv.total() == 0);
    CHECK(totalItems(sim.targets) == 10);
    for (int i = 1; i <= 3; ++i) CHECK(c.entityAt(start + fwd * i)->itemCount() == 0);
}

static void testLinesAllRotations() {
    section("barrel -> belts -> barrel, every rotation");
    for (int r = 0; r < ROTATION_COUNT; ++r) testLine(r);
}

// Where along the line is the single item? 0 = source barrel, 1..3 = belts, 4 = sink.
static int itemCell(Construct& c, glm::ivec3 start, glm::ivec3 fwd) {
    for (int i = 0; i <= 4; ++i)
        if (c.entityAt(start + fwd * i)->itemCount() > 0) return i;
    return -1;
}

static void testTwoPhase() {
    section("two-phase tick: one cell per step, regardless of entity order");
    for (int reversed = 0; reversed < 2; ++reversed) {
        Construct c;
        glm::ivec3 start(0), fwd = blockForward(0);
        if (!reversed) {
            buildLine(c, start, 0, 3);
        } else {   // place the far end first so the entities vector iterates downstream-first
            c.setBlock(start + fwd * 4, makeBlock(BARREL, 0));
            for (int i = 3; i >= 1; --i) c.setBlock(start + fwd * i, makeBlock(CONVEYER_BELT, 0));
            c.setBlock(start, makeBlock(BARREL, 0));
        }
        c.entityAt(start)->inv.insert(ItemId::IronOre, 1);
        FactorySim sim; sim.targets = { &c };
        int prev = itemCell(c, start, fwd);
        CHECK(prev == 0);
        for (int t = 0; t < 80; ++t) {
            sim.tick(FACTORY_TICK_DT);
            int cur = itemCell(c, start, fwd);
            CHECK(cur >= prev && cur - prev <= 1);
            prev = cur;
        }
        CHECK(prev == 4);
    }
}

static void testBeltSpacing() {
    section("belt capacity and spacing");
    Construct c;
    c.setBlock({0,0,0}, makeBlock(BARREL, 0));
    c.setBlock({0,1,0}, makeBlock(CONVEYER_BELT, 0));   // dead-ends into nothing: fills and stalls
    c.entityAt({0,0,0})->inv.insert(ItemId::IronOre, 40);
    FactorySim sim; sim.targets = { &c };
    for (int t = 0; t < 200; ++t) {
        sim.tick(FACTORY_TICK_DT);
        auto& cs = std::get<ConveyorState>(c.entityAt({0,1,0})->ext);
        CHECK(cs.count <= BELT_CAPACITY);
        for (int i = 1; i < cs.count; ++i)
            CHECK(cs.at(i - 1).progress - cs.at(i).progress >= BELT_SPACING - 1e-4f);
        for (int i = 0; i < cs.count; ++i) CHECK(cs.at(i).progress <= 1.0f);
    }
    auto& cs = std::get<ConveyorState>(c.entityAt({0,1,0})->ext);
    CHECK(cs.count == BELT_CAPACITY);
    CHECK(cs.frontReady());
    CHECK(c.entityAt({0,0,0})->inv.total() == 40 - BELT_CAPACITY);
}

static void testSplitter() {
    section("splitter round-robin");
    Construct c;
    // Barrel below feeds the splitter (pull); its three outputs each run one belt into a barrel.
    c.setBlock({0,0,0}, makeBlock(BARREL, 0));
    c.setBlock({0,1,0}, makeBlock(CONVEYER_SPLITTER, 0));
    c.setBlock({0,2,0}, makeBlock(CONVEYER_BELT, 0));  c.setBlock({0,3,0}, makeBlock(BARREL, 0));   // Top  (+Y)
    c.setBlock({-1,1,0}, makeBlock(CONVEYER_BELT, 12)); c.setBlock({-2,1,0}, makeBlock(BARREL, 0)); // Left (-X)
    c.setBlock({1,1,0}, makeBlock(CONVEYER_BELT, 8));  c.setBlock({2,1,0}, makeBlock(BARREL, 0));   // Right(+X)
    CHECK(blockForward(12) == glm::ivec3(-1,0,0));
    c.entityAt({0,0,0})->inv.insert(ItemId::IronOre, 30);
    FactorySim sim; sim.targets = { &c };
    runFor(sim, 8.0f);
    int a = c.entityAt({0,3,0})->inv.total(), b = c.entityAt({-2,1,0})->inv.total(),
        d = c.entityAt({2,1,0})->inv.total();
    CHECK(a + b + d == 30);
    CHECK(a == 10 && b == 10 && d == 10);
    CHECK(totalItems(sim.targets) == 30);
}

static uint32_t portCount(const Construct& c, glm::ivec3 p) {
    return std::get<PortState>(c.entityAt(p)->ext).count;
}

static void testPortRig() {
    section("port in spawns its item, port out sinks and filters");
    Construct c;
    c.setBlock({0,0,0}, makeBlock(PORT_IN, 0));          // Top (+Y) emits
    c.setBlock({0,1,0}, makeBlock(CONVEYER_BELT, 0));
    c.setBlock({0,2,0}, makeBlock(PORT_OUT, 4));         // rotation 4: Top faces -Y, toward the belt
    CHECK(portAt(makeBlock(PORT_IN, 0), Top) == PortMode::Output);
    CHECK(portAt(makeBlock(PORT_OUT, 4), Bottom) == PortMode::Input);
    BlockEntity* in  = c.entityAt({0,0,0});
    BlockEntity* out = c.entityAt({0,2,0});
    FactorySim sim; sim.targets = { &c };

    // No item chosen: nothing is spawned.
    runFor(sim, 1.0f);
    CHECK(portCount(c, {0,0,0}) == 0 && portCount(c, {0,2,0}) == 0);

    // Outside the room ports stay quiet even with an item chosen.
    sim.portsActive = false;
    in->setPortItem(ItemId::IronIngot);
    runFor(sim, 1.0f);
    CHECK(portCount(c, {0,0,0}) == 0 && in->inv.empty());

    // In the room: ~8 items/s reach the sink (minus the belt's 0.5 s transit), none are stored.
    sim.portsActive = true;
    runFor(sim, 3.0f);
    uint32_t got = portCount(c, {0,2,0});
    CHECK(got >= 16 && got <= 26);
    CHECK(out->itemCount() == 0 && out->firstItem() == ItemId::None);
    CHECK(portCount(c, {0,0,0}) >= got);

    // Filter: the sink refuses the wrong item and the belt backs up, losing nothing.
    out->setPortItem(ItemId::CopperIngot);
    runFor(sim, 2.0f);
    CHECK(portCount(c, {0,2,0}) == got);
    CHECK(std::get<ConveyorState>(c.entityAt({0,1,0})->ext).count == BELT_CAPACITY);
    // Matching filter: flow resumes.
    out->setPortItem(ItemId::IronIngot);
    runFor(sim, 1.0f);
    CHECK(portCount(c, {0,2,0}) > got);

    // A sink pulls the item it wants out of a vault holding several kinds.
    Construct v;
    v.setBlock({0,1,0}, makeBlock(CONVEYER_VAULT, 0));   // Top (+Y) emits
    v.setBlock({0,2,0}, makeBlock(PORT_OUT, 4));         // Top faces down onto the vault
    v.entityAt({0,1,0})->inv.insert(ItemId::Scrap, 5);
    v.entityAt({0,1,0})->inv.insert(ItemId::Glass, 3);
    v.entityAt({0,2,0})->setPortItem(ItemId::Glass);
    FactorySim sim2; sim2.targets = { &v };
    runFor(sim2, 2.0f);
    CHECK(portCount(v, {0,2,0}) == 3);
    CHECK(v.entityAt({0,1,0})->inv.count(ItemId::Scrap) == 5);
}

static void testPortIds() {
    section("port IDs: upper case in, lower case out, looked up across the rig");
    Construct c;
    c.setBlock({0,0,0}, makeBlock(PORT_IN, 0));
    c.setBlock({3,0,0}, makeBlock(PORT_OUT, 0));
    FactorySim sim; sim.targets = { &c };
    CHECK(sim.findPortById('A') == nullptr && sim.findPortById(0) == nullptr);
    c.entityAt({0,0,0})->setPortId('A');
    c.entityAt({3,0,0})->setPortId('a');
    CHECK(sim.findPortById('A') == c.entityAt({0,0,0}));
    CHECK(sim.findPortById('a') == c.entityAt({3,0,0}));
    CHECK(sim.findPortById('B') == nullptr);
    c.entityAt({0,0,0})->setPortId(0);
    CHECK(sim.findPortById('A') == nullptr);
}

static void testEnabledAndProps() {
    section("sparse props and the enabled switch");
    BlockEntity e = makeEntity({0,0,0}, makeBlock(PORT_IN, 0));
    CHECK(e.props.empty() && e.enabled() && e.portItem() == ItemId::None && e.portId() == 0);
    e.setEnabled(false);
    CHECK(e.props.entries.size() == 1 && !e.enabled());
    e.setEnabled(true);
    CHECK(e.props.empty());                               // back to default: nothing stored
    e.setPortId('Q'); e.setPortItem(ItemId::Glass); e.setEnabled(false);
    CHECK(e.props.entries.size() == 3);
    e.setPortId(0); e.setPortItem(ItemId::None); e.setEnabled(true);
    CHECK(e.props.empty());

    // A disabled belt stops the line (nothing lost); re-enabling resumes it.
    Construct c;
    buildLine(c, {0,0,0}, 0, 2);
    c.entityAt({0,0,0})->inv.insert(ItemId::Scrap, 10);
    c.entityAt({0,1,0})->setEnabled(false);
    FactorySim sim; sim.targets = { &c };
    runFor(sim, 2.0f);
    CHECK(c.entityAt({0,0,0})->inv.total() == 10);
    CHECK(totalItems(sim.targets) == 10);
    c.entityAt({0,1,0})->setEnabled(true);
    runFor(sim, 3.0f);
    CHECK(c.entityAt({0,3,0})->inv.total() == 10);

    // A disabled barrel refuses pushes: the belt in front of it just waits.
    c.entityAt({0,3,0})->setEnabled(false);
    c.entityAt({0,0,0})->inv.insert(ItemId::Scrap, 4);
    runFor(sim, 3.0f);
    CHECK(c.entityAt({0,3,0})->inv.total() == 10);
    CHECK(totalItems(sim.targets) == 14);
    CHECK(c.entityAt({0,3,0})->insert(ItemId::Scrap, 1) == 0);   // even a direct insert
    c.entityAt({0,3,0})->setEnabled(true);
    runFor(sim, 3.0f);
    CHECK(c.entityAt({0,3,0})->inv.total() == 14);
}

static void testCrossConstruct() {
    section("transfers across construct boundaries");
    Construct a, b;
    a.setBlock({0,0,0}, makeBlock(BARREL, 0));
    a.entityAt({0,0,0})->inv.insert(ItemId::Scrap, 5);
    b.setBlock({0,1,0}, makeBlock(CONVEYER_BELT, 0));
    b.setBlock({0,2,0}, makeBlock(BARREL, 0));
    FactorySim sim; sim.targets = { &a, &b };
    runFor(sim, 3.0f);
    CHECK(b.entityAt({0,2,0})->inv.count(ItemId::Scrap) == 5);
    CHECK(a.entityAt({0,0,0})->inv.empty());
}

static void testTypeFilteredSink() {
    section("a full single-slot barrel of another type refuses, belt backs up, nothing lost");
    Construct c;
    buildLine(c, {0,0,0}, 0, 2);
    c.entityAt({0,0,0})->inv.insert(ItemId::IronOre, 3);
    c.entityAt({0,3,0})->inv.insert(ItemId::Scrap, 1);   // sink's only slot now holds Scrap
    FactorySim sim; sim.targets = { &c };
    runFor(sim, 5.0f);
    CHECK(c.entityAt({0,3,0})->inv.count(ItemId::IronOre) == 0);
    CHECK(totalItems(sim.targets) == 4);
    CHECK(std::get<ConveyorState>(c.entityAt({0,2,0})->ext).frontReady());
    CHECK(describeEntity(*c.entityAt({0,2,0})).find("Iron Ore @1.00") != std::string::npos);
}

// ---- machines, effects, fluids ---------------------------------------------------------

static void testRecipeTable() {
    section("recipe table validates and sizes machine slots");
    std::string problems = validateRecipes();
    if (!problems.empty()) std::printf("%s", problems.c_str());
    CHECK(problems.empty());
    CHECK(recipeTakesOn(SIFTER, ItemId::Gravel, Bottom));
    CHECK(!recipeTakesOn(SIFTER, ItemId::Gravel, Top));
    CHECK(!recipeTakesOn(SIFTER, ItemId::Sand, -1));
    CHECK(recipeOffersOn(SIFTER, ItemId::Sand, Top) && !recipeOffersOn(SIFTER, ItemId::Sand, Back));
    CHECK(recipeOffersOn(SIFTER, ItemId::IronOre, Back) && recipeOffersOn(SIFTER, ItemId::Scrap, Back));
    BlockEntity s = makeEntity({0,0,0}, makeBlock(SIFTER, 0));
    CHECK(std::holds_alternative<MachineState>(s.ext));
    CHECK(s.inv.slotCount == 1 && std::get<MachineState>(s.ext).out.slotCount == 2);
    // Per-face filtering on the entity: gravel only through the Bottom (world -Y at rot 0).
    CHECK(s.acceptable(ItemId::Gravel, Bottom) > 0);
    CHECK(s.acceptable(ItemId::Gravel, Top) == 0);
    CHECK(s.acceptable(ItemId::Sand, Bottom) == 0);
    CHECK(s.acceptable(ItemId::Gravel) > 0);              // any face: something takes it
    // Effect logic hook: a magnet grants MAGNETIC (and speed to a sifter only).
    BlockEntity mg = makeEntity({0,0,1}, makeBlock(MAGNET, 0));
    Effect fx = effectOf(mg, s);
    CHECK(fx.has(EFFECT_MAGNETIC) && fx.speed > 1.0f);
    BlockEntity g = makeEntity({5,0,0}, makeBlock(GRINDER, 0));
    CHECK(effectOf(mg, g).has(EFFECT_MAGNETIC) && effectOf(mg, g).speed == 1.0f);
    Effect both = combineEffects(Effect{ 2.0f, 0.5f, 1 }, Effect{ 1.5f, 0.5f, 2 });
    CHECK(std::abs(both.speed - 3.0f) < 1e-5f && std::abs(both.bonusChance - 0.75f) < 1e-5f && both.flags == 3);
    CHECK(effectFlagsText(0) == "none" && effectFlagsText(EFFECT_MAGNETIC) == "Magnetic");
}

static void testGrinder() {
    section("grinder: limestone in the bottom, gravel out the top, 1:2");
    Construct c;
    c.setBlock({0,0,0}, makeBlock(BARREL, 0));
    c.setBlock({0,1,0}, makeBlock(CONVEYER_BELT, 0));
    c.setBlock({0,2,0}, makeBlock(GRINDER, 0));
    c.setBlock({0,3,0}, makeBlock(CONVEYER_BELT, 0));
    c.setBlock({0,4,0}, makeBlock(BARREL, 0));
    c.entityAt({0,0,0})->inv.insert(ItemId::Limestone, 4);
    FactorySim sim; sim.targets = { &c };
    // 4 crafts x 2 s (speed 1.0) plus belt transit: well under 14 s.
    runFor(sim, 14.0f);
    CHECK(c.entityAt({0,4,0})->inv.count(ItemId::Gravel) == 8);
    CHECK(c.entityAt({0,0,0})->inv.empty());
    CHECK(std::get<MachineState>(c.entityAt({0,2,0})->ext).crafted == 4);
    CHECK(c.entityAt({0,2,0})->itemCount() == 0);
    // Wrong item never enters: the belt carries sand up to the grinder, which refuses it, so
    // it queues on the belt and none is lost.
    c.entityAt({0,0,0})->inv.insert(ItemId::Sand, 3);
    runFor(sim, 3.0f);
    CHECK(c.entityAt({0,2,0})->inv.count(ItemId::Sand) == 0);
    CHECK(c.entityAt({0,0,0})->inv.count(ItemId::Sand) + c.entityAt({0,1,0})->itemCount() == 3);
}

// Gravel barrel -> belt -> Sifter, whose Top and Back outputs are each belted into a barrel;
// magnet optional on its Front. (Machines are passive: a belt has to feed them.)
struct SifterRig {
    Construct c;
    glm::ivec3 feed{0,-1,0}, sifter{0,1,0}, topBarrel{0,3,0}, backBarrel{0,1,-2}, magnet{0,1,1};
    void build(bool withMagnet) {
        c.setBlock(feed, makeBlock(BARREL, 0));
        c.setBlock({0,0,0}, makeBlock(CONVEYER_BELT, 0));
        c.setBlock(sifter, makeBlock(SIFTER, 0));                     // Front = +Z, Back = -Z
        c.setBlock({0,2,0}, makeBlock(CONVEYER_BELT, 0));             // Top output line
        c.setBlock(topBarrel, makeBlock(BARREL, 0));
        int backRot = rotationWith(Top, Back);                        // belt flowing -Z
        c.setBlock({0,1,-1}, makeBlock(CONVEYER_BELT, backRot));      // Back output line
        c.setBlock(backBarrel, makeBlock(BARREL, 0));
        if (withMagnet) {
            int magRot = rotationWith(Bottom, Back);                  // its Effect face toward the sifter
            c.setBlock(magnet, makeBlock(MAGNET, magRot));
        }
        c.entityAt(feed)->inv.insert(ItemId::Gravel, 8);
    }
};

static void testSifterRouting() {
    section("sifter: per-face outputs, magnet unlocks the magnetic recipe");
    {
        SifterRig r; r.build(false);
        FactorySim sim; sim.targets = { &r.c };
        runFor(sim, 12.0f);                              // 4 crafts x 1.5 s (speed 2) + transit
        CHECK(r.c.entityAt(r.topBarrel)->inv.count(ItemId::Sand) == 4);
        CHECK(r.c.entityAt(r.backBarrel)->inv.count(ItemId::Scrap) == 4);
        CHECK(r.c.entityAt(r.backBarrel)->inv.count(ItemId::Sand) == 0);   // sand never leaves the back
        CHECK(r.c.entityAt(r.topBarrel)->inv.count(ItemId::Scrap) == 0);
        CHECK(std::get<MachineState>(r.c.entityAt(r.sifter)->ext).fx.flags == 0);
    }
    {
        SifterRig r; r.build(true);
        CHECK(portAt(*r.c.blockAt(r.magnet), Back) == PortMode::Effect);
        FactorySim sim; sim.targets = { &r.c };
        runFor(sim, 12.0f);
        CHECK(std::get<MachineState>(r.c.entityAt(r.sifter)->ext).fx.has(EFFECT_MAGNETIC));
        CHECK(r.c.entityAt(r.topBarrel)->inv.count(ItemId::Sand) == 4);
        CHECK(r.c.entityAt(r.backBarrel)->inv.count(ItemId::IronOre) == 4);
        CHECK(r.c.entityAt(r.backBarrel)->inv.count(ItemId::Scrap) == 0);
        // Switching the magnet off drops the effect and the recipe with it. (The back barrel is
        // single-slot and full of iron ore, so empty it first or it would refuse the scrap.)
        r.c.entityAt(r.magnet)->setEnabled(false);
        r.c.entityAt(r.backBarrel)->inv.clear();
        r.c.entityAt(r.feed)->inv.insert(ItemId::Gravel, 2);
        runFor(sim, 6.0f);
        CHECK(!std::get<MachineState>(r.c.entityAt(r.sifter)->ext).fx.has(EFFECT_MAGNETIC));
        CHECK(r.c.entityAt(r.backBarrel)->inv.count(ItemId::Scrap) == 1);
    }
}

static void testFluids() {
    section("fluids: pipe port -> pipes -> tank, fluids only, colour variant re-meshes");
    Construct c;
    c.setBlock({0,0,0}, makeBlock(PIPE_PORT_IN, 0));
    c.setBlock({0,1,0}, makeBlock(PIPE_PLUMBING, 0));
    c.setBlock({0,2,0}, makeBlock(PIPE_PLUMBING, 0));
    c.setBlock({0,3,0}, makeBlock(PIPE_TANK, 0));
    BlockEntity* src  = c.entityAt({0,0,0});
    BlockEntity* pipe = c.entityAt({0,1,0});
    BlockEntity* tank = c.entityAt({0,3,0});
    CHECK(pipe->acceptable(ItemId::IronIngot) == 0 && tank->acceptable(ItemId::Gravel) == 0);
    CHECK(pipe->acceptable(ItemId::Water, Bottom) > 0 && tank->acceptable(ItemId::Oil) > 0);
    CHECK(src->insert(ItemId::IronIngot, 1) == 0);         // fluid ports carry fluids only
    CHECK(visualVariant(*pipe) == 0 && visualVariant(*tank) == 0);

    FactorySim sim; sim.targets = { &c };
    src->setPortItem(ItemId::Water);
    runFor(sim, 4.0f);
    uint16_t stored = tank->inv.count(ItemId::Water);
    CHECK(stored >= 12 && stored <= 32);                   // 8 units/s through two pipes
    CHECK(tank->inv.count(ItemId::Oil) == 0);
    CHECK(visualVariant(*tank) == fluidVariant(ItemId::Water));
    CHECK(c.meshDirty);                                    // the look changed: owner re-meshes
    CHECK(pipe->visualKey == 1 && tank->visualKey == 1);
    c.meshDirty = false;
    runFor(sim, 1.0f);
    CHECK(!c.meshDirty);                                   // steady flow: a pipe keeps its tint
    CHECK(pipe->visualKey == 1);
    // A pipe holding water refuses oil until it empties.
    if (pipe->inv.count(ItemId::Water) > 0) CHECK(pipe->acceptable(ItemId::Oil, Bottom) == 0);
    // Pipe Port Out sinks fluid from a tank behind it.
    c.setBlock({0,4,0}, makeBlock(PIPE_PORT_OUT, 4));      // Top (Input) faces down onto the tank
    runFor(sim, 4.0f);
    CHECK(portCount(c, {0,4,0}) > 0);
    CHECK(tank->inv.count(ItemId::Water) + portCount(c, {0,4,0}) >= stored);
}

int main() {
    testRotationSet();
    testPorts();
    testCanTransfer();
    testInventory();
    testEntityStore();
    testLinesAllRotations();
    testTwoPhase();
    testBeltSpacing();
    testSplitter();
    testPortRig();
    testPortIds();
    testEnabledAndProps();
    testCrossConstruct();
    testTypeFilteredSink();
    testRecipeTable();
    testGrinder();
    testSifterRouting();
    testFluids();
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
