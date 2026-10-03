#include "yorehold/framework/map/Pathfinding.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace yh
{

namespace
{

struct Node
{
    float cost = 0;
    int diagonals = 0;
};

struct OpenEntry
{
    float priority;
    float cost;
    Cell cell;
    bool operator>(const OpenEntry& o) const { return priority > o.priority; }
};

}

std::vector<Cell> findPath(const Grid& grid, Cell start, Cell goal, const std::function<bool(Cell)>& passable, int maxVisited)
{
    if (start == goal)
        return {start};
    if (!passable(goal))
        return {};

    // Many routes on a grid cost exactly the same. Without a tie-breaker A* picks an arbitrary one,
    // which looks like a drunk zig-zag. Nudging the priority by how far a cell strays from the
    // straight start-goal line makes it pick the route a person would draw.
    const float lineX = static_cast<float>(goal.x - start.x);
    const float lineY = static_cast<float>(goal.y - start.y);
    const float lineLength = std::max(1.0f, std::sqrt(lineX * lineX + lineY * lineY));
    auto straying = [&](Cell c) {
        const float cx = static_cast<float>(c.x - start.x);
        const float cy = static_cast<float>(c.y - start.y);
        return std::abs(cx * lineY - cy * lineX) / lineLength;
    };

    std::priority_queue<OpenEntry, std::vector<OpenEntry>, std::greater<>> open;
    std::unordered_map<Cell, Node, CellHash> best; // cheapest known way to each cell
    std::unordered_map<Cell, Cell, CellHash> cameFrom;
    best[start] = {0, 0};
    open.push({grid.distance(start, goal), 0, start});

    std::vector<Cell> neighbours;
    int visited = 0;
    bool found = false;
    while (!open.empty() && visited < maxVisited)
    {
        const OpenEntry entry = open.top();
        open.pop();
        const Node node = best[entry.cell];
        if (entry.cost > node.cost + 1e-4f)
            continue; // a cheaper way here was found after this entry was queued
        if (entry.cell == goal)
        {
            found = true;
            break;
        }
        visited++;

        const Cell current = entry.cell;
        grid.neighbours(current, neighbours);
        for (const Cell next : neighbours)
        {
            if (!passable(next))
                continue;
            const bool diagonal = grid.type() != GridType::Hex && next.x != current.x && next.y != current.y;
            // No cutting corners past walls or creatures.
            if (diagonal && (!passable({next.x, current.y}) || !passable({current.x, next.y})))
                continue;

            const float cost = node.cost + grid.stepCost(current, next, node.diagonals);
            const auto known = best.find(next);
            if (known != best.end() && known->second.cost <= cost)
                continue;
            best[next] = {cost, node.diagonals + (diagonal ? 1 : 0)};
            cameFrom[next] = current;
            open.push({cost + grid.distance(next, goal) + straying(next) * 0.001f, cost, next});
        }
    }

    if (!found)
        return {};

    std::vector<Cell> path{goal};
    for (Cell at = goal; at != start;)
    {
        at = cameFrom[at];
        path.push_back(at);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

}
