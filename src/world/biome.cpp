#include "world/biome.h"

#include "core/block.h"
#include "core/constants.h"
#include "world/noise.h"
#include "world/terrain.h"

// static const unsigned char* g_biomeMap = nullptr;
// static int g_biomeMapW = 0, g_biomeMapH = 0;

const std::vector<Biome>& biomeTable() {
    static const std::vector<Biome> table = [] {
        std::vector<Biome> t;
        // GENERAL / WARM — temperate: beaches, grassy lowlands, soil, rock, snow caps.
        t.push_back({ "general", {0, 255, 0}, {
            { SAND,  BEACH_TOP_M, BEACH_TRANS_M },
            { GRASS, 1100.0, 220.0 },
            { DIRT,  2000.0, 260.0 },
            { STONE, 5000.0, 260.0 },
            { SNOW,  0.0,    0.0   },
        }});
        // COLD — short grass, quick to bare rock, low snow line.
        t.push_back({ "cold", {255, 255, 255}, {
            { SAND,  BEACH_TOP_M, BEACH_TRANS_M },
            { GRASS, 250.0, 90.0  },
            { STONE, 650.0, 160.0 },
            { SNOW,  0.0,   0.0   },
        }});
        // TROPIC — lush: grass climbs high, snow only on the tallest peaks.
        t.push_back({ "tropic", {0, 100, 0}, {
            { SAND,  BEACH_TOP_M, BEACH_TRANS_M },
            { GRASS, 2600.0, 320.0 },
            { DIRT,  3700.0, 320.0 },
            { STONE, 6000.0, 320.0 },
            { SNOW,  0.0,    0.0   },
        }});
        // DESERT — sand far up the slopes, bare rock mesas, rare snowy summits.
        t.push_back({ "desert", {255, 255, 0}, {
            { SAND,  1600.0, 320.0 },
            { STONE, 6000.0, 360.0 },
            { SNOW,  0.0,    0.0   },
        }});
        return t;
    }();
    return table;
}

int nearestBiome(int r, int g, int b) {
    if (r + g + b < 24) return 0;
    const std::vector<Biome>& table = biomeTable();
    int best = 0, bestD = 1 << 30;
    for (int i = 0; i < (int)table.size(); ++i) {
        glm::ivec3 c = table[i].color;
        int dr = r - c.r, dg = g - c.g, db = b - c.b;
        int d = dr * dr + dg * dg + db * db;
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

int biomeAt(int wx, int wz) {
    if (!g_biomeMap) return 0;
    const int W = g_biomeMapW, H = g_biomeMapH;
    double u = (double)wx / WORLD_WIDTH_BLOCKS * W;
    double v = (double)wz / WORLD_DEPTH_BLOCKS * H;
    v = (H - 1) - v;                                  // match the heightmap's north/south orientation
    int x = (int)std::floor(u); x = ((x % W) + W) % W;                   // wrap longitude
    int y = (int)std::floor(v); y = y < 0 ? 0 : (y >= H ? H - 1 : y);    // clamp latitude
    const unsigned char* p = g_biomeMap + ((size_t)y * W + x) * 3;
    return nearestBiome(p[0], p[1], p[2]);
}

int surfaceBlock(double heightM, int wx, int wz) {
    const std::vector<BiomeBand>& bands = biomeTable()[biomeAt(wx, wz)].bands;
    if (bands.empty()) return GRASS;

    const double r = columnHash01(wx, wz);   // per-column dither value, [0, 1)
    // Wander the upper boundaries so they don't trace exact contours; the beach edge
    // (band 0 -> 1) is left un-jittered so coastlines stay tight to the water.
    const double jitter = noise::fbm(wx * BIOME_FREQ, wz * BIOME_FREQ, 3, 2.0, 0.5) * BIOME_JITTER_M;

    int id = bands[0].block;
    for (size_t i = 0; i + 1 < bands.size(); ++i) {
        double edge = bands[i].topM;
        double half = bands[i].transM > 0.0 ? bands[i].transM : 1.0;
        double h    = (i == 0) ? heightM : heightM + jitter;
        double t    = (h - (edge - half)) / (2.0 * half);
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        if (r < t) id = bands[i + 1].block;   // dithered crossing into the next band up
    }
    return id;
}