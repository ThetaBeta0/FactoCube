#pragma once

#include <cstddef>
#include <functional>
#include <tuple>

#include <glm/glm.hpp>

// Hash for world-block positions (unordered_map<glm::ivec3, ...> keys).
struct IVec3Hash {
    size_t operator()(const glm::ivec3& v) const noexcept {
        size_t seed = std::hash<int>{}(v.x);
        seed ^= std::hash<int>{}(v.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<int>{}(v.z) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct tuple_hash {
    template <typename Tuple>
    size_t operator ()(const Tuple& t) const {
        size_t seed = 0;
        // Fold over every element of the tuple (works for 3- and 4-element keys).
        std::apply([&seed](const auto&... elems) {
            ((hash_combine(seed, elems)), ...);
        }, t);
        return seed;
    }

private:
    template <typename T>
    static void hash_combine(size_t& seed, const T& v) {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);  // Combine the hash values
    }
};