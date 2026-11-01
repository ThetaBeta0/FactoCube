#pragma once

#include <cmath>

#include "core/constants.h"
#include "world/noise.h"

inline double sampleHeightmap01(const unsigned char* data, int w, int h, double u, double v) {
    u -= std::floor(u / w) * w;          // wrap longitude into [0, w)
    if (v < 0.0) v = 0.0;
    if (v > h - 1) v = h - 1;
    int x0 = (int)std::floor(u);
    int x1 = (x0 + 1) % w;
    int y0 = (int)std::floor(v);
    int y1 = y0 + 1 < h ? y0 + 1 : y0;
    double fx = u - x0;
    double fy = v - y0;
    auto px = [&](int xx, int yy) { return (double)data[yy * w + xx]; };
    double a = noise::lerp(px(x0, y0), px(x1, y0), fx);
    double b = noise::lerp(px(x0, y1), px(x1, y1), fx);
    return noise::lerp(a, b, fy) / 255.0;
}

inline double terrainHeightBlocks(const unsigned char* hm, int w, int h, int wx, int wz) {
    // Map the Earth-sized world onto the heightmap (fractional texels).
    double u = (double)wx / WORLD_WIDTH_BLOCKS * w;
    double v = (double)wz / WORLD_DEPTH_BLOCKS * h;
    v = (h - 1) - v;                      // preserve original north/south orientation

    double base01 = sampleHeightmap01(hm, w, h, u, v);
    double baseM  = base01 * MAX_ELEVATION_M;

    // Fractal detail: rugged at high elevations, gentle in the lowlands.
    double n   = noise::fbm(wx * DETAIL_FREQ, wz * DETAIL_FREQ, DETAIL_OCTAVES, 2.0, DETAIL_GAIN);
    double amp = DETAIL_AMP_MIN + base01 * DETAIL_AMP_RANGE;
    return baseM + n * amp;
}

inline double columnHash01(int wx, int wz) {
    unsigned h = (unsigned)wx * 73856093u ^ (unsigned)wz * 19349663u;
    h ^= h >> 13; h *= 0x85ebca6bu; h ^= h >> 16;
    return (h & 0xffffffu) / (double)0x1000000;
}

unsigned char* loadHeightMap(const char* path, int& width, int& height);