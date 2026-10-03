#include "yorehold/framework/map/LightLevels.h"

#include <algorithm>
#include <cmath>

namespace yh
{

LightLevels::LightLevels(int width, int height, float cellSize)
    : width_(std::max(1, width)), height_(std::max(1, height)), cellSize_(cellSize > 0 ? cellSize : 1),
      fixed_(static_cast<size_t>(width_) * height_, LightLevel::Dark)
{
}

LightLevel LightLevels::from(const Light& light, Vec2 center, std::span<const Wall> walls) const
{
    if (light.radius <= 0 || !std::isfinite(light.radius)) return LightLevel::Dark;
    const Vec2 delta = center - light.position;
    const float distance2 = delta.x * delta.x + delta.y * delta.y;
    if (distance2 > light.radius * light.radius) return LightLevel::Dark;
    if (light.shadows && !lineOfSight(light.position, center, walls)) return LightLevel::Dark;
    const float bright = light.radius * std::clamp(brightFraction, 0.0f, 1.0f);
    return distance2 <= bright * bright ? LightLevel::Bright : LightLevel::Dim;
}

void LightLevels::setFixed(std::span<const Light> lights, std::span<const Wall> walls)
{
    std::fill(fixed_.begin(), fixed_.end(), LightLevel::Dark);
    for (const Light& light : lights)
    {
        if (light.radius <= 0 || !std::isfinite(light.radius)) continue;
        const int x0 = std::clamp(static_cast<int>(std::floor((light.position.x - light.radius) / cellSize_)), 0, width_ - 1);
        const int x1 = std::clamp(static_cast<int>(std::floor((light.position.x + light.radius) / cellSize_)), 0, width_ - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor((light.position.y - light.radius) / cellSize_)), 0, height_ - 1);
        const int y1 = std::clamp(static_cast<int>(std::floor((light.position.y + light.radius) / cellSize_)), 0, height_ - 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                LightLevel& cell = fixed_[static_cast<size_t>(y) * width_ + x];
                if (cell == LightLevel::Bright) continue;
                cell = std::max(cell, from(light, {(x + 0.5f) * cellSize_, (y + 0.5f) * cellSize_}, walls));
            }
    }
}

LightLevel LightLevels::level(Cell cell, std::span<const Light> moving, std::span<const Wall> walls) const
{
    if (cell.x < 0 || cell.y < 0 || cell.x >= width_ || cell.y >= height_) return ambient;
    LightLevel best = std::max(ambient, fixed_[static_cast<size_t>(cell.y) * width_ + cell.x]);
    const Vec2 center{(cell.x + 0.5f) * cellSize_, (cell.y + 0.5f) * cellSize_};
    for (const Light& light : moving)
    {
        if (best == LightLevel::Bright) break;
        best = std::max(best, from(light, center, walls));
    }
    return best;
}

}
