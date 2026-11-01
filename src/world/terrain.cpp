#include "world/terrain.h"

#include <iostream>

#include <stb-master/stb_image.h>

unsigned char* loadHeightMap(const char* path, int& width, int& height) {
    // Load the height map image (grayscale)
    unsigned char* data = stbi_load(path, &width, &height, nullptr, 1);  // 1 for grayscale
    if (!data) {
        std::cerr << "Failed to load heightmap!" << std::endl;
    }
    return data;
}