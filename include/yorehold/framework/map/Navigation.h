#pragma once

#include "yorehold/framework/map/Pathfinding.h"

#include <unordered_map>

namespace yh
{

// Cached connected components inside each chunk form a coarse graph. A route through that
// graph restricts the final cell A* to a narrow corridor, keeping long walks inexpensive.
// Call invalidate(cell) when a wall/door/tile changes, or clear() after wholesale edits.
class Navigation
{
public:
    Navigation(Grid grid, int width, int height, std::function<bool(Cell)> passable, int chunkSize = 32);
    std::vector<Cell> path(Cell start, Cell goal, int maxVisited = 50000, int maxChunks = 4096);
    void invalidate(Cell cell);
    void clear() { chunks_.clear(); }
    size_t cachedChunks() const { return chunks_.size(); }

private:
    struct Chunk { std::vector<int> components; };
    bool inside(Cell cell) const;
    bool canStep(Cell from, Cell to) const;
    Cell chunkAt(Cell cell) const { return {cell.x / chunkSize_, cell.y / chunkSize_}; }
    const Chunk& chunk(Cell cell);
    int component(Cell cell);
    Grid grid_;
    int width_, height_, chunkSize_;
    std::function<bool(Cell)> passable_;
    std::unordered_map<Cell, Chunk, CellHash> chunks_;
};

}
