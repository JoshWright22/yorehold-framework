#pragma once

#include <yorehold/framework/graphics/Renderer.h>
#include <yorehold/framework/map/TileMap.h>

#include <algorithm>
#include <cmath>
#include <execution>
#include <numeric>
#include <cstdint>
#include <vector>

// Placeholder art and terrain for the map test scenes: a generated 8-tile tileset and noise-based terrain.
namespace testterrain
{

enum Tile : yh::TileId
{
    DeepWater = 1,
    Water,
    Sand,
    Grass,
    Forest,
    Rock,
    Snow,
    Tree, // decor layer, transparent around the tree
};

inline uint32_t hash(int x, int y, uint32_t seed)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

inline float random01(int x, int y, uint32_t seed)
{
    return static_cast<float>(hash(x, y, seed) & 0xFFFFFF) / static_cast<float>(0x1000000);
}

// Smooth value noise with a few octaves, roughly 0-1.
inline float noise(float x, float y, uint32_t seed)
{
    float total = 0;
    float amplitude = 0.5f;
    float frequency = 1.0f / 64.0f;
    for (int octave = 0; octave < 5; octave++)
    {
        const float fx = x * frequency;
        const float fy = y * frequency;
        const int ix = static_cast<int>(std::floor(fx));
        const int iy = static_cast<int>(std::floor(fy));
        float tx = fx - ix;
        float ty = fy - iy;
        tx = tx * tx * (3 - 2 * tx);
        ty = ty * ty * (3 - 2 * ty);
        const uint32_t s = seed + octave;
        const float a = random01(ix, iy, s), b = random01(ix + 1, iy, s);
        const float c = random01(ix, iy + 1, s), d = random01(ix + 1, iy + 1, s);
        total += amplitude * ((a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty);
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return total / 0.96875f;
}

inline yh::TileId terrainAt(int x, int y, uint32_t seed)
{
    const float h = noise(static_cast<float>(x), static_cast<float>(y), seed);
    if (h < 0.30f) return DeepWater;
    if (h < 0.38f) return Water;
    if (h < 0.42f) return Sand;
    if (h < 0.60f) return Grass;
    if (h < 0.72f) return Forest;
    if (h < 0.84f) return Rock;
    return Snow;
}

inline bool walkable(yh::TileId tile)
{
    return tile == Sand || tile == Grass || tile == Forest || tile == Snow;
}

inline yh::Tileset makeTileset(yh::Renderer& renderer)
{
    constexpr int px = 32;
    constexpr int columns = 4;
    constexpr int rows = 2;
    const yh::Color base[8] = {
        {24, 52, 110, 255}, {44, 92, 160, 255}, {214, 196, 140, 255}, {86, 150, 70, 255},
        {46, 104, 52, 255}, {118, 112, 108, 255}, {232, 236, 240, 255}, {28, 80, 38, 255},
    };

    std::vector<unsigned char> pixels(columns * px * rows * px * 4, 0);
    yh::Tileset tileset;
    tileset.columns = columns;
    tileset.rows = rows;
    tileset.tilePixels = px;
    for (int t = 0; t < 8; t++)
    {
        const int ox = (t % columns) * px;
        const int oy = (t / columns) * px;
        float r = 0, g = 0, b = 0;
        for (int y = 0; y < px; y++)
        {
            for (int x = 0; x < px; x++)
            {
                const float shade = 0.88f + 0.24f * random01(x / 2, y / 2, 100 + t);
                unsigned char* p = &pixels[((oy + y) * columns * px + ox + x) * 4];
                p[0] = static_cast<unsigned char>(std::min(255.0f, base[t].r * shade));
                p[1] = static_cast<unsigned char>(std::min(255.0f, base[t].g * shade));
                p[2] = static_cast<unsigned char>(std::min(255.0f, base[t].b * shade));
                p[3] = 255;
                if (t == Tree - 1)
                {
                    // A round treetop on transparent.
                    const float dx = x - px / 2.0f + 0.5f, dy = y - px / 2.0f + 0.5f;
                    p[3] = dx * dx + dy * dy < (px * 0.42f) * (px * 0.42f) ? 255 : 0;
                }
                r += p[0];
                g += p[1];
                b += p[2];
            }
        }
        const float n = static_cast<float>(px * px);
        tileset.averageColors.push_back({static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n), static_cast<uint8_t>(b / n), 255});
    }
    tileset.texture = renderer.createTexture(columns * px, rows * px, pixels.data());
    return tileset;
}

// Fills `ground` with terrain and sprinkles trees over forest on `decor`.
// The noise is the slow part, so rows are computed in parallel first, then stored.
inline void generate(yh::TileMap& map, int ground, int decor, uint32_t seed)
{
    const int width = map.width();
    std::vector<yh::TileId> tiles(static_cast<size_t>(width) * map.height());
    std::vector<int> rows(map.height());
    std::iota(rows.begin(), rows.end(), 0);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < width; x++)
            tiles[static_cast<size_t>(y) * width + x] = terrainAt(x, y, seed);
    });

    for (int y = 0; y < map.height(); y++)
    {
        for (int x = 0; x < width; x++)
        {
            const yh::TileId tile = tiles[static_cast<size_t>(y) * width + x];
            map.setTile(ground, x, y, tile);
            if (decor >= 0 && tile == Forest && random01(x, y, seed + 7) < 0.35f)
                map.setTile(decor, x, y, Tree);
        }
    }
}

}