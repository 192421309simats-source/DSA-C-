#ifndef IMAGE_HASH_H
#define IMAGE_HASH_H

#include <string>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION_GUARD // just a marker; actual STB impl toggled once in main.cpp
#include "../vendor/stb_image.h"

// ImageHash
// ---------
// Computes a 64-bit "average hash" (aHash) for an uploaded photo — a classical,
// explainable perceptual-hashing technique (NOT a trained ML model, and we
// don't claim otherwise): decode the image, shrink it to an 8x8 grayscale
// thumbnail, compare each pixel to the average brightness, and pack the
// 64 above/below-average bits into a hash. Two images that look visually
// similar (same item, different lighting/angle) produce hashes with a small
// Hamming distance; unrelated photos differ in roughly half their bits.
namespace ImageHash {

    // Returns true and fills outHash on success; false if the image could
    // not be decoded (corrupt upload, unsupported format, etc).
    inline bool computeFromMemory(const unsigned char* data, int len, uint64_t& outHash) {
        int width = 0, height = 0, channels = 0;
        // Force 1 channel (grayscale) output directly from stb_image.
        unsigned char* pixels = stbi_load_from_memory(data, len, &width, &height, &channels, 1);
        if (!pixels) return false;

        // Downscale to 8x8 using simple nearest-neighbor sampling — sufficient
        // for an average hash, and keeps the implementation dependency-free.
        const int N = 8;
        double small[N][N];
        double total = 0.0;
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                int srcX = std::min(width - 1, (x * width) / N);
                int srcY = std::min(height - 1, (y * height) / N);
                unsigned char v = pixels[srcY * width + srcX];
                small[y][x] = static_cast<double>(v);
                total += v;
            }
        }
        stbi_image_free(pixels);

        double average = total / (N * N);
        uint64_t hash = 0;
        int bit = 0;
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                if (small[y][x] >= average) hash |= (1ULL << bit);
                bit++;
            }
        }
        outHash = hash;
        return true;
    }

    inline std::string toHex(uint64_t hash) {
        std::ostringstream oss;
        oss << std::hex << std::setw(16) << std::setfill('0') << hash;
        return oss.str();
    }

    inline uint64_t fromHex(const std::string& hex) {
        if (hex.empty()) return 0;
        uint64_t v = 0;
        std::istringstream iss(hex);
        iss >> std::hex >> v;
        return v;
    }

    inline int hammingDistance(uint64_t a, uint64_t b) {
        uint64_t x = a ^ b;
#if defined(__GNUG__) || defined(__clang__)
        return __builtin_popcountll(x);
#else
        int count = 0;
        while (x) { count += (x & 1); x >>= 1; }
        return count;
#endif
    }

    // 0.0 - 1.0 similarity: 64 matching bits -> 1.0, 32 (random/unrelated) -> ~0.5,
    // 0 matching bits -> 0.0.
    inline double similarity(uint64_t a, uint64_t b) {
        int dist = hammingDistance(a, b);
        return 1.0 - (static_cast<double>(dist) / 64.0);
    }
}

#endif // IMAGE_HASH_H
