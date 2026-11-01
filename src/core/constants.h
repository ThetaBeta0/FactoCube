#pragma once

#include <glm/glm.hpp>

constexpr int WORLD_SIZE = 48;
constexpr int CHUNK_SIZE = 16;
constexpr float WORLD_SCALE = 1.0f;
constexpr float WORLD_HEIGHT = 1.0f;

constexpr int SEA_LEVEL = 13;

constexpr int MAX_LOD = 8;   // coarsest level: voxels of 2^8 = 256 m (reaches ~25 km)
constexpr int LOD_RING_CHUNKS = 7;   // how many LOD-L chunks deep before switching to L+1 (detail persistence)
constexpr int LOD_LOAD_RADIUS = 8;   // per-LOD chunk loop half-extent (>= LOD_RING_CHUNKS)
constexpr int LOD_VBAND_BELOW = 3;   // chunks loaded below each column's surface
constexpr int LOD_VBAND_ABOVE = 1;   // chunks loaded above each column's surface
constexpr int SKIRT_VOX = 1;
constexpr int MAX_UPLOADS_PER_FRAME = 48;

constexpr float NEAR_PLANE = 0.1f;
constexpr float FAR_PLANE = 60000.0f;
constexpr float LOD_DEPTH_BIAS = 6e-5f;
constexpr float FOG_START = 6000.0f;
constexpr float FOG_END = 30000.0f;

constexpr double BEACH_TOP_M    = 13.0 + 6.0;   // sand strip just above the waterline (SEA_LEVEL+6)
constexpr double BEACH_TRANS_M  = 4.0;
constexpr double BIOME_FREQ     = 1.0 / 4000.0; // wavelength of biome-edge wander (~4 km)
constexpr double BIOME_JITTER_M = 150.0;

constexpr double WORLD_WIDTH_BLOCKS = 40075017.0;   // x span: equatorial circumference (m)
constexpr double WORLD_DEPTH_BLOCKS = 20037508.0;   // z span: pole-to-pole meridian (m)
constexpr double MAX_ELEVATION_M    = 8849.0;       // heightmap value 255 maps to this height (m)
constexpr double DETAIL_FREQ        = 1.0 / 2500.0; // base detail wavelength ~2.5 km
constexpr int    DETAIL_OCTAVES     = 7;            // finest features ~40 m
constexpr double DETAIL_GAIN        = 0.55;         // >0.5 keeps more high-frequency roughness
constexpr double DETAIL_AMP_MIN     = 16.0;         // gentle hills (m) even in lowlands
constexpr double DETAIL_AMP_RANGE   = 950.0;     

// ---- Water (see render/water.h; all tuning lives here) -------------------------------
// Rendered height of the still sea surface. Blocks are drawn CENTRED on their coordinates,
// so a shoreline block whose snapped top is SEA_LEVEL shows its top face at SEA_LEVEL - 0.5;
// the water sits a hair below that so beaches get a small lip instead of the water hovering
// half a block above the sand. WATER_SURFACE_DIP is the size of that lip (blocks).
constexpr float WATER_SURFACE_DIP = 0.1f;
constexpr float WATER_SURFACE_Y   = (float)SEA_LEVEL - 0.5f - WATER_SURFACE_DIP;
// Transparency of the sea surface: 0.0 = fully opaque, 1.0 = invisible. 0.10 = 10%.
constexpr float WATER_TRANSPARENCY   = 0.10f;
constexpr float WATER_WAVE_AMPLITUDE = 0.14f;  // peak vertical bob of open-water nodes (blocks)
constexpr float WATER_WAVE_SPEED     = 1.0f;   // global time multiplier for the wave animation
// Waves fade out between these camera distances (blocks). FADE_END must stay below the first
// LOD ring boundary (LOD_RING_CHUNKS * CHUNK_SIZE): beyond it every LOD's water is exactly
// flat at WATER_SURFACE_Y, so overlapping LOD rings stay coplanar and never crack or double-blend.
constexpr float WATER_WAVE_FADE_START = 40.0f;
constexpr float WATER_WAVE_FADE_END   = 80.0f;
// The wave field repeats every WATER_WAVE_PERIOD blocks. Chunk origins are passed to the
// shader modulo this period, keeping float precision at planet scale while staying seamless
// across chunk borders (all shader wave frequencies are integer multiples of 2*pi/period).
constexpr float WATER_WAVE_PERIOD = 512.0f;
// Atlas position of the 2x2 block of water tiles: (0,13),(1,13),(0,14),(1,14). Each 1x1
// world block hashes its coordinates to pick one of the four, breaking up repetition.
constexpr int WATER_TILE_BASE_X = 0;
constexpr int WATER_TILE_BASE_Y = 13;

constexpr double EDIT_REACH = 6.0;

// A placed block joins the nearest construct whose grid is within this many blocks
// (Chebyshev distance to its bounding box); otherwise it starts a brand-new construct.
constexpr int CONSTRUCT_MERGE_DISTANCE = 8;

// ---- Factory simulation (see sim/factory.h) ----------------------------------------
// Machines advance in fixed steps so item flow is frame-rate independent and deterministic.
constexpr float FACTORY_TICK_RATE = 20.0f;              // simulation steps per second
constexpr float FACTORY_TICK_DT   = 1.0f / FACTORY_TICK_RATE;
constexpr int   FACTORY_MAX_STEPS_PER_FRAME = 8;        // catch-up cap after a stall
// Items a single belt cell can carry; also sets the spacing between items (1/BELT_CAPACITY).
constexpr int   BELT_CAPACITY = 4;

// ---- Menus (see ui/block_menu.h) ------------------------------------------------------
inline const glm::vec4 MENU_COLOR        = glm::vec4(0x62 / 255.0f, 0x62 / 255.0f, 0x62 / 255.0f, 1.0f); // #626262
inline const glm::vec4 MENU_BORDER_COLOR = glm::vec4(0x3a / 255.0f, 0x3a / 255.0f, 0x3a / 255.0f, 1.0f); // #3a3a3a
constexpr float MENU_SLOT_PX  = 64.0f;   // slot size at a 900px-tall framebuffer (scales with height)
constexpr float MENU_PAD_PX   = 12.0f;
constexpr int   MENU_SLOT_COLS = 4;      // slots per row
constexpr float MENU_CURSOR_PX = 28.0f;  // pointer sprite size; its top-left corner is the hotspot
constexpr float MENU_BUTTON_W  = 260.0f; // pause-menu buttons at a 900px-tall framebuffer
constexpr float MENU_BUTTON_H  = 48.0f;
// The sheet the pause screen lays over the world: menu grey at 70%.
inline const glm::vec4 PAUSE_OVERLAY_COLOR = glm::vec4(0x62 / 255.0f, 0x62 / 255.0f, 0x62 / 255.0f, 0.7f);
constexpr int   MENU_PICKER_COLS = 8;    // item picker grid width
constexpr double MENU_ID_CAPTURE_SECONDS = 5.0;   // how long a port waits for its ID letter
// FactoCubeItemTextures.png UI tiles: slot frame, missing-art tile, pointer, plus, minus.
constexpr int ITEM_SLOT_TILE_X   = 0, ITEM_SLOT_TILE_Y   = 15;
constexpr int ITEM_ERROR_TILE_X  = 0, ITEM_ERROR_TILE_Y  = 0;
constexpr int ITEM_CURSOR_TILE_X = 1, ITEM_CURSOR_TILE_Y = 0;
constexpr int ITEM_PLUS_TILE_X   = 2, ITEM_PLUS_TILE_Y   = 0;
constexpr int ITEM_MINUS_TILE_X  = 3, ITEM_MINUS_TILE_Y  = 0;

// ---- Blueprint mode (N in build mode) -----------------------------------------------
// The design room spans [-BLUEPRINT_HALF, +BLUEPRINT_HALF] on every axis, centred on the
// origin (so 2*50+1 = 101 cells per side); anything outside counts as a solid wall.
constexpr int BLUEPRINT_HALF       = 50;
constexpr int BLUEPRINT_MAX_VOLUME = 50;   // most blocks a single blueprint may contain
constexpr int BLUEPRINT_TILE_X     = 2;    // atlas tile every wall block of the room shows
constexpr int BLUEPRINT_TILE_Y     = 14;
// Free placement in the room: when the crosshair ray hits nothing within reach, the ghost
// floats this many blocks ahead so a component can be started in mid-air.
constexpr double BLUEPRINT_AIR_REACH = 4.0;

inline int idealLOD(double distBlocks) {
    for (int L = 0; L < MAX_LOD; ++L) {
        if (distBlocks < (double)LOD_RING_CHUNKS * (double)(CHUNK_SIZE << L)) return L;
    }
    return MAX_LOD;
}

inline int positiveMod(int value, int modulus) {
    return ((value % modulus) + modulus) % modulus;
}