#pragma once

#include "yorehold/framework/map/Grid.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace yh
{

// An area effect on the map: fire, grease, water, ice. Each cell can hold multiple surfaces of
// different types. Surfaces persist through rounds and are removed when their duration expires.
struct Surface
{
    std::string id;      // fire, grease, water, ice, or custom
    int roundsLeft = 1;  // how long it persists
    float radius = 1.0f; // the area affected, in squares, from the origin cell

    bool expired() const { return roundsLeft <= 0; }
    void endRound() { --roundsLeft; }
};

// Grid of surfaces on the map. Each cell can have multiple surfaces of different types.
// Manages creation, removal, interactions (water extinguishes fire) and round-end cleanup.
class Surfaces
{
public:
    Surfaces(int width, int height, float cellSize);

    int width() const { return width_; }
    int height() const { return height_; }
    float cellSize() const { return cellSize_; }

    // Add a surface to the map. `at` is the origin cell, `id` is the surface type,
    // `radius` is how far it spreads in cells, `rounds` is how long it lasts.
    void create(Cell at, std::string_view id, float radius, int rounds);

    // Get all surfaces at a cell.
    const std::vector<Surface>& at(Cell cell) const;

    // Get all surfaces on the map.
    const std::unordered_map<Cell, std::vector<Surface>, CellHash>& all() const { return surfaces_; }

    // Remove all surfaces at a cell (e.g., when interactions clear them).
    void clearAt(Cell cell) { surfaces_.erase(cell); }

    // Process the end of a round: decrement durations and remove expired surfaces.
    // Returns surfaces that expired, grouped by cell.
    std::unordered_map<Cell, std::vector<std::string>, CellHash> endRound();

    // Check if a surface type exists at a cell.
    bool has(Cell cell, std::string_view id) const;

    // Remove a specific surface type from a cell (used for interactions like water extinguishing fire).
    bool remove(Cell cell, std::string_view id);

    // Clear all surfaces from the map.
    void clear() { surfaces_.clear(); }

    // Serialization for saves.
    std::string toJson() const;
    bool fromJson(std::string_view json, std::string* error = nullptr);

private:
    void addToAllCells(Cell center, float radius, const Surface& surface);

    int width_, height_;
    float cellSize_;
    std::unordered_map<Cell, std::vector<Surface>, CellHash> surfaces_;
    static const std::vector<Surface> empty_;
};

}
