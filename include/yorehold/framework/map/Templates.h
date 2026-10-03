#pragma once

#include "yorehold/framework/map/Grid.h"

#include <span>
#include <vector>

namespace yh
{

enum class TemplateShape : uint8_t
{
    Circle, // radius `size` around the origin (fireball)
    Cone,   // from the origin toward `direction`, `size` long, `angleDegrees` wide (breath weapons)
    Line,   // from the origin toward `direction`, `size` long, `width` wide (lightning bolt)
    Square, // `size` wide, centred on the origin, axis-aligned (cubes, auras)
};

// A spell or effect area placed on the map, measured in world units. A cell is covered when its
// centre is inside the shape (the Foundry/5e template rule), so results match what players see.
struct AreaTemplate
{
    TemplateShape shape = TemplateShape::Circle;
    Vec2 origin;
    Vec2 direction{1, 0}; // any length; only the angle matters
    float size = 0;
    float width = 0;      // lines only; 0 means one grid cell wide
    float angleDegrees = 53.13f; // 5e cones are as wide as they are long at the far end

    // Aims a cone or line from `origin` at `target`.
    static AreaTemplate aimed(TemplateShape shape, Vec2 origin, Vec2 target, float size);

    bool contains(Vec2 point, float lineWidth) const;
    std::vector<Cell> cells(const Grid& grid) const;
    // Closed outline for drawing (circles and cone arcs use `segments` points).
    std::vector<Vec2> outline(const Grid& grid, int segments = 48) const;
    void draw(Renderer& renderer, const Grid& grid, Color fill, Color edge, float zoom = 1) const;
};

// A ruler through waypoints. Distances follow the grid's movement rules (diagonal rules carry
// across waypoints, so 5-10-5 stays correct around corners).
struct Measurement
{
    std::vector<float> legs; // cells per segment
    float total = 0;         // cells
};
Measurement measure(const Grid& grid, std::span<const Vec2> waypoints);
// Draws the ruler with a label at each waypoint (`unitsPerCell`, e.g. 5 for feet).
void drawRuler(Renderer& renderer, const Grid& grid, std::span<const Vec2> waypoints, float unitsPerCell, const char* unit, Color color, float zoom = 1);

}
