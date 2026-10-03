#include "yorehold/framework/map/Templates.h"

#include "yorehold/framework/graphics/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace yh
{

namespace
{

constexpr float pi = 3.14159265f;
constexpr float sqrt3 = 1.7320508f;

float length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
Vec2 unit(Vec2 v) { const float l = length(v); return l > 0 ? v / l : Vec2{1, 0}; }

// Every cell whose centre could fall inside `area`, for any grid shape.
template <typename Visit>
void forCellsNear(const Grid& grid, const Rect& area, Visit visit)
{
    if (grid.type() == GridType::Hex)
    {
        const float rowHeight = grid.size() * sqrt3 / 2;
        const int firstRow = static_cast<int>(std::floor(area.y / rowHeight)) - 1;
        const int lastRow = static_cast<int>(std::ceil((area.y + area.h) / rowHeight)) + 1;
        for (int r = firstRow; r <= lastRow; ++r)
        {
            const int firstQ = static_cast<int>(std::floor(area.x / grid.size() - r / 2.0f)) - 1;
            const int lastQ = static_cast<int>(std::ceil((area.x + area.w) / grid.size() - r / 2.0f)) + 1;
            for (int q = firstQ; q <= lastQ; ++q) visit(Cell{q, r});
        }
        return;
    }
    const Cell from = grid.cellAt(area.position()), to = grid.cellAt(area.position() + area.size());
    for (int y = from.y; y <= to.y; ++y)
        for (int x = from.x; x <= to.x; ++x) visit(Cell{x, y});
}

}

AreaTemplate AreaTemplate::aimed(TemplateShape shape, Vec2 origin, Vec2 target, float size)
{
    AreaTemplate t;
    t.shape = shape;
    t.origin = origin;
    t.direction = target - origin;
    t.size = size;
    return t;
}

bool AreaTemplate::contains(Vec2 point, float lineWidth) const
{
    const Vec2 d = point - origin;
    const float slack = std::max(size, 1.0f) * 1e-4f; // centres exactly on the edge count as inside
    switch (shape)
    {
    case TemplateShape::Circle: return length(d) <= size + slack;
    case TemplateShape::Square: return std::abs(d.x) <= size / 2 + slack && std::abs(d.y) <= size / 2 + slack;
    case TemplateShape::Cone:
    {
        const float distance = length(d);
        if (distance <= slack) return true;
        if (distance > size + slack) return false;
        const Vec2 u = unit(direction);
        const float cosine = (d.x * u.x + d.y * u.y) / distance;
        return cosine >= std::cos(std::clamp(angleDegrees, 0.0f, 360.0f) * pi / 360.0f) - 1e-5f;
    }
    case TemplateShape::Line:
    {
        const Vec2 u = unit(direction);
        const float along = d.x * u.x + d.y * u.y;
        const float across = std::abs(d.x * u.y - d.y * u.x);
        return along >= -slack && along <= size + slack && across <= lineWidth / 2 + slack;
    }
    }
    return false;
}

std::vector<Cell> AreaTemplate::cells(const Grid& grid) const
{
    const float lineWidth = width > 0 ? width : grid.size();
    const float reach = shape == TemplateShape::Square ? size / 2 : shape == TemplateShape::Line ? size + lineWidth : size;
    const Rect bounds{origin.x - reach, origin.y - reach, reach * 2, reach * 2};
    std::vector<Cell> result;
    forCellsNear(grid, bounds, [&](Cell c) { if (contains(grid.center(c), lineWidth)) result.push_back(c); });
    return result;
}

std::vector<Vec2> AreaTemplate::outline(const Grid& grid, int segments) const
{
    std::vector<Vec2> points;
    const Vec2 u = unit(direction), side{-u.y, u.x};
    switch (shape)
    {
    case TemplateShape::Circle:
        for (int i = 0; i < segments; ++i)
        {
            const float a = 2 * pi * static_cast<float>(i) / static_cast<float>(segments);
            points.push_back(origin + Vec2{std::cos(a), std::sin(a)} * size);
        }
        break;
    case TemplateShape::Square:
        points = {origin + Vec2{-size / 2, -size / 2}, origin + Vec2{size / 2, -size / 2}, origin + Vec2{size / 2, size / 2}, origin + Vec2{-size / 2, size / 2}};
        break;
    case TemplateShape::Cone:
    {
        points.push_back(origin);
        const float half = std::clamp(angleDegrees, 0.0f, 360.0f) * pi / 360.0f, aim = std::atan2(u.y, u.x);
        const int arc = std::max(2, segments / 3);
        for (int i = 0; i <= arc; ++i)
        {
            const float a = aim - half + 2 * half * static_cast<float>(i) / static_cast<float>(arc);
            points.push_back(origin + Vec2{std::cos(a), std::sin(a)} * size);
        }
        break;
    }
    case TemplateShape::Line:
    {
        const Vec2 halfWidth = side * ((width > 0 ? width : grid.size()) / 2);
        points = {origin + halfWidth, origin + u * size + halfWidth, origin + u * size - halfWidth, origin - halfWidth};
        break;
    }
    }
    return points;
}

void AreaTemplate::draw(Renderer& renderer, const Grid& grid, Color fill, Color edge, float zoom) const
{
    const auto points = outline(grid);
    if (points.size() < 3) return;
    // Fan from a point inside the (convex) shape so each pixel is filled once.
    const Vec2 u = unit(direction);
    const Vec2 inside = shape == TemplateShape::Cone ? origin + u * (size * 0.5f)
                      : shape == TemplateShape::Line ? origin + u * (size * 0.5f)
                                                     : origin;
    renderer.fillFan(inside, points, fill, fill);
    for (size_t i = 0; i < points.size(); ++i)
        renderer.drawLine(points[i], points[(i + 1) % points.size()], edge, 2 / std::max(zoom, 0.01f));
}

Measurement measure(const Grid& grid, std::span<const Vec2> waypoints)
{
    Measurement m;
    int diagonalsSoFar = 0;
    for (size_t i = 1; i < waypoints.size(); ++i)
    {
        float leg = 0;
        if (grid.type() == GridType::Gridless) leg = length(waypoints[i] - waypoints[i - 1]) / grid.size();
        else
        {
            const Cell a = grid.cellAt(waypoints[i - 1]), b = grid.cellAt(waypoints[i]);
            leg = grid.distance(a, b);
            if (grid.type() == GridType::Square && grid.diagonals == DiagonalRule::Alternating)
            {
                // Every second diagonal of the whole route costs 2, not every second within this leg.
                const int dx = std::abs(a.x - b.x), dy = std::abs(a.y - b.y), diagonal = std::min(dx, dy);
                leg = static_cast<float>(std::max(dx, dy) + (diagonalsSoFar + diagonal) / 2 - diagonalsSoFar / 2);
                diagonalsSoFar += diagonal;
            }
        }
        m.legs.push_back(leg);
        m.total += leg;
    }
    return m;
}

void drawRuler(Renderer& renderer, const Grid& grid, std::span<const Vec2> waypoints, float unitsPerCell, const char* unit, Color color, float zoom)
{
    if (waypoints.empty()) return;
    const float pixel = 1 / std::max(zoom, 0.01f);
    const Measurement m = measure(grid, waypoints);
    float total = 0;
    for (size_t i = 0; i < waypoints.size(); ++i)
    {
        if (i > 0)
        {
            renderer.drawLine(waypoints[i - 1], waypoints[i], {0, 0, 0, 160}, 5 * pixel);
            renderer.drawLine(waypoints[i - 1], waypoints[i], color, 3 * pixel);
            total += m.legs[i - 1];
        }
        renderer.fillCircle(waypoints[i], 5 * pixel, color, 16);
        if (i == 0) continue;
        const float value = total * unitsPerCell;
        char label[48];
        if (std::abs(value - std::round(value)) < 0.05f) std::snprintf(label, sizeof label, "%.0f %s", value, unit);
        else std::snprintf(label, sizeof label, "%.1f %s", value, unit);
        const float scale = 2 * pixel;
        const Vec2 at = waypoints[i] + Vec2{10 * pixel, -10 * pixel - Renderer::lineHeight(scale)};
        renderer.fillRect({at.x - 4 * pixel, at.y - 3 * pixel, Renderer::textWidth(label, scale) + 8 * pixel, Renderer::lineHeight(scale) + 4 * pixel}, {0, 0, 0, 180});
        renderer.drawText(at, label, {255, 255, 255, 255}, scale);
    }
}

}
