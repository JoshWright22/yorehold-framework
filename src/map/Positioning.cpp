#include "yorehold/framework/map/Positioning.h"

#include <cmath>

namespace yh
{

bool isFlanked(const Grid& grid, Cell target, std::span<const Cell> foes, float reach, const RayBlocked& blocked)
{
    const Vec2 centre = grid.center(target);
    for (size_t i = 0; i < foes.size(); i++)
    {
        if (foes[i] == target || grid.distance(foes[i], target) > reach + 0.01f) continue;
        const Vec2 a = grid.center(foes[i]);
        if (blocked && blocked(a, centre)) continue;
        for (size_t j = i + 1; j < foes.size(); j++)
        {
            if (foes[j] == target || grid.distance(foes[j], target) > reach + 0.01f) continue;
            const Vec2 b = grid.center(foes[j]);
            if (blocked && blocked(b, centre)) continue;
            const Vec2 first = a - centre, second = b - centre;
            const float dot = first.x * second.x + first.y * second.y;
            const float cross = first.x * second.y - first.y * second.x;
            if (dot < 0 && std::abs(cross) <= 0.001f * std::abs(dot)) return true;
        }
    }
    return false;
}

Cover coverBetween(const Grid& grid, Cell from, Cell target, const RayBlocked& blocked)
{
    if (!blocked || from == target) return Cover::None;
    const Vec2 start = grid.center(from), centre = grid.center(target);
    const float inset = grid.size() * 0.4f;
    std::vector<Vec2> corners;
    if (grid.type() == GridType::Hex)
    {
        const float radius = grid.size() * 0.8f / std::sqrt(3.0f);
        corners = {{0, -radius}, {inset, -radius / 2}, {inset, radius / 2},
            {0, radius}, {-inset, radius / 2}, {-inset, -radius / 2}};
    }
    else
        corners = {{-inset, -inset}, {inset, -inset}, {inset, inset}, {-inset, inset}};
    size_t hits = 0;
    for (const Vec2 offset : corners)
        if (blocked(start, centre + offset)) hits++;
    if (hits == 0) return Cover::None;
    if (hits == corners.size()) return Cover::Full;
    return hits * 4 >= corners.size() * 3 ? Cover::ThreeQuarters : Cover::Half;
}

}
