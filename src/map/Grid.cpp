#include "yorehold/framework/map/Grid.h"

#include "yorehold/framework/graphics/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace yh
{

namespace
{

constexpr float sqrt3 = 1.7320508f;

// Rounds fractional axial hex coordinates to the nearest hex (via cube coordinates).
Cell roundHex(float q, float r)
{
    const float s = -q - r;
    float rq = std::round(q);
    float rr = std::round(r);
    const float rs = std::round(s);
    const float dq = std::abs(rq - q);
    const float dr = std::abs(rr - r);
    const float ds = std::abs(rs - s);
    if (dq > dr && dq > ds)
        rq = -rr - rs;
    else if (dr > ds)
        rr = -rq - rs;
    return {static_cast<int>(rq), static_cast<int>(rr)};
}

}

const char* toString(DiagonalRule rule)
{
    switch (rule)
    {
    case DiagonalRule::Equal: return "1-1-1";
    case DiagonalRule::Alternating: return "1-2-1";
    case DiagonalRule::Euclidean: return "1.4";
    case DiagonalRule::Double: return "2";
    }
    return "?";
}

DiagonalRule next(DiagonalRule rule)
{
    return static_cast<DiagonalRule>((static_cast<int>(rule) + 1) % 4);
}

Cell Grid::cellAt(Vec2 world) const
{
    if (type_ == GridType::Hex)
    {
        const float r = world.y / (size_ * sqrt3 / 2.0f);
        const float q = world.x / size_ - r / 2.0f;
        return roundHex(q, r);
    }
    return {static_cast<int>(std::floor(world.x / size_)), static_cast<int>(std::floor(world.y / size_))};
}

Vec2 Grid::center(Cell cell) const
{
    if (type_ == GridType::Hex)
        return {size_ * (static_cast<float>(cell.x) + static_cast<float>(cell.y) / 2.0f), size_ * sqrt3 / 2.0f * static_cast<float>(cell.y)};
    return {(static_cast<float>(cell.x) + 0.5f) * size_, (static_cast<float>(cell.y) + 0.5f) * size_};
}

Vec2 Grid::snap(Vec2 world) const
{
    return type_ == GridType::Gridless ? world : center(cellAt(world));
}

void Grid::neighbours(Cell cell, std::vector<Cell>& out) const
{
    out.clear();
    if (type_ == GridType::Hex)
    {
        static constexpr Cell directions[6] = {{1, 0}, {1, -1}, {0, -1}, {-1, 0}, {-1, 1}, {0, 1}};
        for (const Cell d : directions)
            out.push_back({cell.x + d.x, cell.y + d.y});
        return;
    }
    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            if (dx != 0 || dy != 0)
                out.push_back({cell.x + dx, cell.y + dy});
        }
    }
}

float Grid::distance(Cell a, Cell b) const
{
    const int dx = std::abs(a.x - b.x);
    const int dy = std::abs(a.y - b.y);
    switch (type_)
    {
    case GridType::Hex:
        return static_cast<float>(dx + dy + std::abs(a.x + a.y - b.x - b.y)) / 2.0f;
    case GridType::Gridless:
        return std::sqrt(static_cast<float>(dx * dx + dy * dy));
    case GridType::Square:
        break;
    }
    const int diagonal = std::min(dx, dy);
    const int straight = std::max(dx, dy) - diagonal;
    switch (diagonals)
    {
    case DiagonalRule::Alternating:
        return static_cast<float>(straight + diagonal + diagonal / 2);
    case DiagonalRule::Euclidean:
        return static_cast<float>(straight) + static_cast<float>(diagonal) * 1.41421356f;
    case DiagonalRule::Double:
        return static_cast<float>(straight + diagonal * 2);
    case DiagonalRule::Equal:
        break;
    }
    return static_cast<float>(straight + diagonal);
}

float Grid::stepCost(Cell from, Cell to, int diagonalsSoFar) const
{
    const bool diagonal = type_ != GridType::Hex && from.x != to.x && from.y != to.y;
    if (!diagonal)
        return 1.0f;
    if (type_ == GridType::Gridless)
        return 1.41421356f;
    switch (diagonals)
    {
    case DiagonalRule::Alternating:
        return diagonalsSoFar % 2 == 1 ? 2.0f : 1.0f; // every second diagonal costs double
    case DiagonalRule::Euclidean:
        return 1.41421356f;
    case DiagonalRule::Double:
        return 2.0f;
    case DiagonalRule::Equal:
        break;
    }
    return 1.0f;
}

void Grid::draw(Renderer& renderer, const Rect& area, float zoom, Color color) const
{
    if (type_ == GridType::Gridless || size_ * zoom < 8.0f)
        return;

    const float line = 1.0f / zoom; // always one screen pixel wide

    if (type_ == GridType::Square)
    {
        const float left = std::floor(area.x / size_) * size_;
        const float top = std::floor(area.y / size_) * size_;
        for (float x = left; x <= area.x + area.w; x += size_)
            renderer.fillRect({x, area.y, line, area.h}, color);
        for (float y = top; y <= area.y + area.h; y += size_)
            renderer.fillRect({area.x, y, area.w, line}, color);
        return;
    }

    // Hex: draw three edges per cell (the other three belong to neighbours), over every cell touching the area.
    const float radius = size_ / sqrt3;
    const Vec2 corners[6] = {
        {0, -radius}, {size_ / 2, -radius / 2}, {size_ / 2, radius / 2}, {0, radius}, {-size_ / 2, radius / 2}, {-size_ / 2, -radius / 2},
    };
    const float rowHeight = size_ * sqrt3 / 2.0f;
    const int firstRow = static_cast<int>(std::floor(area.y / rowHeight)) - 1;
    const int lastRow = static_cast<int>(std::ceil((area.y + area.h) / rowHeight)) + 1;
    for (int r = firstRow; r <= lastRow; r++)
    {
        const int firstQ = static_cast<int>(std::floor(area.x / size_ - r / 2.0f)) - 1;
        const int lastQ = static_cast<int>(std::ceil((area.x + area.w) / size_ - r / 2.0f)) + 1;
        for (int q = firstQ; q <= lastQ; q++)
        {
            const Vec2 c = center({q, r});
            for (int i = 0; i < 3; i++)
                renderer.drawLine(c + corners[i], c + corners[i + 1], color, line);
        }
    }
}

}
