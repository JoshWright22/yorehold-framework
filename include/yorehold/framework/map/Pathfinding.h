#pragma once

#include "yorehold/framework/map/Grid.h"

#include <functional>
#include <vector>

namespace yh
{

// A* over any Grid. `passable` says whether a cell can be entered.
// Returns the cells from start to goal (both included), or empty if there's no way there
// within `maxVisited` cells (which keeps a click on an unreachable spot from stalling the frame).
std::vector<Cell> findPath(const Grid& grid, Cell start, Cell goal, const std::function<bool(Cell)>& passable,
    int maxVisited = 50000);

}
