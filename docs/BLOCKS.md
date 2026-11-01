# Adding blocks, machines, effects and fluids

Everything a block *is* lives in one table row; everything it *does* is picked by its
`BlockBehavior`. Most new blocks need only a table row. Machines add a recipe or two,
Additional blocks add one `case` in `effectOf()`, and that's the whole framework.

`make test` runs the headless simulation suite (`tests/sim_test.cpp`) — the fastest way to
check a new block moves items the way you meant.

## 1. The table row (`src/core/block.h`)

```cpp
enum BlockType : uint8_t { ..., MY_BLOCK, BLOCK_TYPE_COUNT };   // 1. give it an id

// tiles:  Front     Back      Left      Right     Top       Bottom
// ports:  Front     Back      Left      Right     Top       Bottom
{ "My Block",   { {x,y},   {x,y},   {x,y},   {x,y},   {x,y},   {x,y}   },   // 2. faces
                { pm::N,   pm::N,   pm::N,   pm::N,   pm::O,   pm::I   },   // 3. ports
  BlockBehavior::Machine, 0, 0, 1.0f, true },                               // 4. behaviour
```

The `static_assert` under the table refuses to build until the enum and the table agree.

Faces are **local**: the block is authored unrotated, flowing along its own +Y
(`Bottom` in, `Top` out) and the rotation system carries that into the world. Nothing
outside `portAt()` ever needs to think about rotation.

Ports:

| PortMode | meaning |
|---|---|
| `I` Input  | transport blocks may push into this face |
| `O` Output | transport blocks may pull from / this block pushes out of this face |
| `A` Any    | both (barrels, tanks) |
| `E` Effect | an Additional block touching this face with its own Effect face grants an effect |
| `N` None   | nothing crosses this face |

The trailing numbers are `slotCount, slotCapacity, itemsPerSecond, ticks [, fluid, variantFaces]`:

| field | transport / storage | machine |
|---|---|---|
| `slotCount` | inventory slots | ignored — sized from its recipes |
| `slotCapacity` | items (or fluid units) per slot | per-slot capacity, `0` = 64 |
| `itemsPerSecond` | throughput | **crafting speed multiplier** |
| `ticks` | `true` for anything that moves or crafts | `true` |
| `fluid` | carries LIQUID items only | — |
| `variantFaces` | faces whose tile column shifts by the contents (see §5) | — |

## 2. Behaviours (`src/core/block.h`, `src/sim/factory.cpp`, `src/sim/entity.cpp`)

| behaviour | what it does |
|---|---|
| `Storage`  | passive item store; belts push in and pull out |
| `Conveyor` | belt: positioned items, pushes ahead, pulls from the passive block behind |
| `Splitter` | small buffer, round-robins its Output faces |
| `Pipe`     | fluid belt: bulk units, pushes ahead, pulls from the passive block behind |
| `Tank`     | passive fluid store |
| `Machine`  | crafts recipes; passive on the network (belts feed it and empty it) |
| `Additional` | grants an Effect through matching Effect ports |
| `PortIn` / `PortOut` | test-rig source / sink (fluid twins when `fluid = true`) |

Two rules make transfers unambiguous: **transport blocks push and pull, everything else is
passive**, and **every tick gathers intents read-only before applying them**, so an item
moves at most one block per step no matter what order entities are stored in.

A brand-new *kind* of behaviour is a new enum entry plus a `case` in `FactorySim::advance`
(private per-tick work), `FactorySim::gather` (what it pushes / pulls) and the item protocol
in `entity.cpp` (`acceptable`, `insert`, `extract`, `available`, `offerFor`). Look at
`Conveyor` for positioned items or `Pipe` for bulk transfer as templates.

## 3. Machines: recipes (`src/sim/recipe.h`)

A machine's behaviour is entirely data:

```cpp
{ SIFTER, "Magnetic sift", 3.0f, EFFECT_MAGNETIC,
  { { Bottom, ItemId::Gravel, 2 } },                                 // inputs
  { { Top, ItemId::Sand, 1 }, { Back, ItemId::IronOre, 1 } } },      // outputs
```

* Each input / output names the **local face** it uses, the item and the count. The counts
  *are* the ratio (2 Gravel → 1 Sand + 1 Iron Ore). `ANY_FACE` means any port of that
  direction.
* A face used by a recipe must carry the matching port in the table; `validateRecipes()`
  reports mistakes to `log.txt` at startup and fails `make test`.
* `seconds` is the base craft time; the block's `itemsPerSecond` multiplier and any effects
  divide it.
* The fourth field lists `EffectFlag`s the machine must currently have. **List recipes that
  need effects before the plain ones**: the first recipe whose flags, inputs and output room
  are all satisfied is the one that runs.
* Inputs are only accepted through the faces the recipes name, and only items some recipe
  takes; outputs only leave through their bound face. So a belt on the Sifter's Back can
  only ever carry away the heavy fraction.

The machine's slots come from the recipes: one input slot per input of its largest recipe,
likewise for outputs (`MachineState::out`). The menu shows both rows, a progress bar, the
active recipe and the effects in play.

`onCraftComplete()` in `src/sim/logic.cpp` is the hook for anything a recipe can't say —
a side product every Nth craft, wear, counters. Add a `case` for the machine.

## 4. Additional blocks: effects (`src/sim/logic.cpp`)

```cpp
Effect effectOf(const BlockEntity& additional, const BlockEntity& machine) {
    Effect fx;
    switch ((BlockType)additional.type) {
        case MAGNET:
            fx.flags |= EFFECT_MAGNETIC;            // unlocks recipes that require it
            if (machine.type == SIFTER) fx.speed = 1.25f;
            break;
        case HEATER:                                // a new one
            fx.flags |= EFFECT_HEATED;
            fx.bonusChance = 0.10f;                 // 10% chance of +1 of every output
            break;
    }
    return fx;
}
```

An `Effect` is a speed multiplier, a bonus-output chance and a set of flags. Several
Additional blocks stack: speeds multiply, chances combine as independent rolls, flags union.
New capabilities are new bits in `EffectFlag` (and a name in `effectFlagsText`).

Placement rule: the Additional block's **Effect face must touch the machine's Effect face**
(both sides `pm::E`). The Magnet's Effect face is its Bottom; the Sifter's is its Front.
Effects are re-scanned every tick, so disabling or removing the magnet takes hold at once.

## 5. Fluids

Set `fluid = true` and the block only ever accepts LIQUID items (`ItemType::LIQUID`); the
item picker on a fluid port shows fluids only. Pipes are bulk belts: `itemsPerSecond` units
per second, `slotCapacity` units buffered, pushing out of Output faces and pulling from a
passive block (tank, machine output, pipe port) behind an Input face. Tanks are passive.

`variantFaces` makes a block show its contents: for the faces in the mask the tile column is
shifted by `fluidVariant(slot 0's fluid)` — plain `+0`, water `+1`, oil `+2`, mud `+3`,
slag `+4`, molten metal `+5`. The pipe uses `FACES_SIDES` (its caps stay), the tank
`FACES_ALL`. When the shown fluid changes the sim flags the construct and it is re-meshed
that frame (a pipe that has just drained keeps its last tint until a different fluid comes,
so a trickling line doesn't flicker). Any other "looks different when X" can reuse this:
return a different `visualVariant()` and give the block a row of tiles.

## 6. Ports, IDs and per-block settings

Port In / Port Out (and the pipe twins) are the room's test rig: a source spawns its chosen
item at `itemsPerSecond` while the room simulates; a sink consumes what matches its chosen
item (or anything) and counts it. Each carries an ID letter — upper-case for In, lower-case
for Out, unique per rig — which is how a blueprint's inputs/outputs will be referred to.

Per-block settings (`PropKey` in `src/sim/entity.h`) are stored sparsely: only non-default
values exist, so an untouched block carries nothing. Add a key, a default in `propDefault`,
and typed accessors on `BlockEntity`; the menu is where players set them.

## 7. Checklist for a new machine

1. Tiles in the atlas; `BlockType` id; table row with `BlockBehavior::Machine`, Input /
   Output (and Effect) ports on the faces you want, `itemsPerSecond` = speed multiplier.
2. Recipes in `recipeTable`, faces matching the ports.
3. (optional) an Additional block: table row with an Effect port + a `case` in `effectOf`.
4. (optional) a `case` in `onCraftComplete`.
5. `make test` — add a rig like `testSifterRouting` for it; `make run` to try it in the room.
