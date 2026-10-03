#pragma once

#include "yorehold/framework/graphics/Types.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace yh
{

class Renderer;

// A grid cell. Square grids use column/row; hex grids use axial (q, r) coordinates.
struct Cell
{
    int x = 0;
    int y = 0;

    constexpr bool operator==(const Cell&) const = default;
};

struct CellHash
{
    size_t operator()(Cell c) const noexcept
    {
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32) | static_cast<uint32_t>(c.y);
        return std::hash<uint64_t>()(key);
    }
};

enum class GridType
{
    Square,
    Hex,      // pointy-top hexes
    Gridless, // free movement; cells still exist (squares of `size`) for pathfinding
};

// How diagonal steps count on square grids.
enum class DiagonalRule
{
    Equal,       // every diagonal step costs 1 (5e default)
    Alternating, // 1, 2, 1, 2... (the 5-10-5 rule)
    Euclidean,   // ~1.41, true distance
    Double,      // every diagonal costs 2 (same as two straight steps)
};

const char* toString(DiagonalRule rule);
DiagonalRule next(DiagonalRule rule);

// Everything the rest of the framework needs from a grid, whatever its shape.
// Positions are world units; `size` is the distance between neighbouring cell centres.
class Grid
{
public:
    Grid(GridType type, float size) : type_(type), size_(size) {}

    GridType type() const { return type_; }
    float size() const { return size_; }
    DiagonalRule diagonals = DiagonalRule::Equal;

    Cell cellAt(Vec2 world) const;
    Vec2 center(Cell cell) const;
    // Where a dragged or dropped thing lands. Gridless maps don't snap.
    Vec2 snap(Vec2 world) const;
    void neighbours(Cell cell, std::vector<Cell>& out) const;
    // Movement distance in cells, following the grid's rules.
    float distance(Cell a, Cell b) const;
    // Cost of one step between neighbours (diagonals can cost more).
    float stepCost(Cell from, Cell to, int diagonalsSoFar) const;

    // Draws grid lines over `area` (world units). Skips drawing when cells are too small on screen to read.
    void draw(Renderer& renderer, const Rect& area, float zoom, Color color) const;

private:
    GridType type_;
    float size_;
};

}
