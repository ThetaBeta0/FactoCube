#pragma once

#include <vector>

#include <glm/glm.hpp>

struct BiomeBand {
    int block;    // BlockType the surface wears within this band
    double topM;     // band spans up to this elevation (world m); ignored for the last band
    double transM;   // half-width of the dithered fade into the band above
};
struct Biome {
    const char* name;
    glm::ivec3 color;   // key colour in biomeMap.png (0..255 RGB)
    std::vector<BiomeBand> bands;   // ordered low -> high; the last band extends up forever
};

static const unsigned char* g_biomeMap;
static int g_biomeMapW = 0, g_biomeMapH = 0;

const std::vector<Biome>& biomeTable();

int nearestBiome(int r, int g, int b);

int biomeAt(int wx, int wz);

int surfaceBlock(double heightM, int wx, int wz);