#pragma once

#include "yorehold/framework/graphics/Lighting.h"
#include "yorehold/framework/map/Grid.h"

#include <span>
#include <vector>

namespace yh
{

enum class LightLevel { Dark, Dim, Bright };

// How well lit each cell is, for rules that care (seeing in the dark, hiding). A light is bright
// out to `brightFraction` of its radius and dim to the edge; walls block it. Lights that never
// move go in once with setFixed(); carried ones are passed every time to level().
class LightLevels
{
public:
    LightLevels(int width, int height, float cellSize);

    LightLevel ambient = LightLevel::Dark; // where no light reaches
    float brightFraction = 0.5f;

    void setFixed(std::span<const Light> lights, std::span<const Wall> walls);
    // Fixed lights plus `moving` ones (torches in hand), which are tested on the spot.
    LightLevel level(Cell cell, std::span<const Light> moving = {}, std::span<const Wall> walls = {}) const;
    bool lit(Cell cell, std::span<const Light> moving = {}, std::span<const Wall> walls = {}) const
    {
        return level(cell, moving, walls) != LightLevel::Dark;
    }

private:
    LightLevel from(const Light& light, Vec2 center, std::span<const Wall> walls) const;

    int width_, height_;
    float cellSize_;
    std::vector<LightLevel> fixed_;
};

}
