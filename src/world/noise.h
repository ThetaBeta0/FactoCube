#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace noise {
    // Permutation table, lazily built once from a fixed seed (deterministic terrain).
    inline const int* permTable() {
        static int perm[512];
        static const bool init = []() {
            std::array<int, 256> p;
            for (int i = 0; i < 256; ++i) p[i] = i;
            std::mt19937 rng(1337u);
            std::shuffle(p.begin(), p.end(), rng);
            for (int i = 0; i < 512; ++i) perm[i] = p[i & 255];
            return true;
        }();
        (void)init;
        return perm;
    }
    inline double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    inline double lerp(double a, double b, double t) { return a + t * (b - a); }
    inline double grad(int hash, double x, double y) {
        int h = hash & 7;
        double u = h < 4 ? x : y;
        double v = h < 4 ? y : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -2.0 * v : 2.0 * v);
    }
    // Ken Perlin's improved noise, 2D. Returns roughly [-1, 1].
    inline double perlin(double x, double y) {
        const int* perm = permTable();
        int X = (int)std::floor(x) & 255;
        int Y = (int)std::floor(y) & 255;
        x -= std::floor(x);
        y -= std::floor(y);
        double u = fade(x), v = fade(y);
        int A = perm[X] + Y, B = perm[X + 1] + Y;
        return lerp(lerp(grad(perm[A], x, y),         grad(perm[B], x - 1, y),     u),
                    lerp(grad(perm[A + 1], x, y - 1),  grad(perm[B + 1], x - 1, y - 1), u), v);
    }
    // Fractal Brownian motion (summed octaves), normalised to roughly [-1, 1].
    inline double fbm(double x, double y, int octaves, double lacunarity, double gain) {
        double sum = 0.0, amp = 1.0, freq = 1.0, norm = 0.0;
        for (int i = 0; i < octaves; ++i) {
            sum  += amp * perlin(x * freq, y * freq);
            norm += amp;
            amp  *= gain;
            freq *= lacunarity;
        }
        return norm > 0.0 ? sum / norm : 0.0;
    }
}