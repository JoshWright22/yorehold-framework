#include "yorehold/framework/map/Navigation.h"

#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>

namespace yh
{

Navigation::Navigation(Grid grid, int width, int height, std::function<bool(Cell)> passable, int chunkSize)
    : grid_(grid), width_(width), height_(height), chunkSize_(chunkSize), passable_(std::move(passable))
{
    if (width <= 0 || height <= 0 || chunkSize < 2 || chunkSize > 256 || !passable_)
        throw std::invalid_argument("Invalid navigation dimensions or callback");
}

bool Navigation::inside(Cell c) const { return c.x >= 0 && c.y >= 0 && c.x < width_ && c.y < height_; }
bool Navigation::canStep(Cell a, Cell b) const
{
    if (!inside(b) || !passable_(b)) return false;
    const bool diagonal = grid_.type() != GridType::Hex && a.x != b.x && a.y != b.y;
    return !diagonal || (passable_({a.x, b.y}) && passable_({b.x, a.y}));
}

const Navigation::Chunk& Navigation::chunk(Cell key)
{
    if (const auto it = chunks_.find(key); it != chunks_.end()) return it->second;
    Chunk result;
    result.components.assign(static_cast<size_t>(chunkSize_) * chunkSize_, -1);
    const Cell origin{key.x * chunkSize_, key.y * chunkSize_};
    auto index = [&](Cell c) { return (c.y - origin.y) * chunkSize_ + c.x - origin.x; };
    int label = 0;
    std::vector<Cell> queue, neighbours;
    for (int y = 0; y < chunkSize_; ++y)
        for (int x = 0; x < chunkSize_; ++x)
        {
            const Cell start{origin.x + x, origin.y + y};
            if (!inside(start) || !passable_(start) || result.components[index(start)] >= 0) continue;
            queue.clear();
            queue.push_back(start);
            result.components[index(start)] = label;
            for (size_t head = 0; head < queue.size(); ++head)
            {
                const Cell current = queue[head];
                grid_.neighbours(current, neighbours);
                for (Cell next : neighbours)
                    if (inside(next) && chunkAt(next) == key && result.components[index(next)] < 0 && canStep(current, next))
                    {
                        result.components[index(next)] = label;
                        queue.push_back(next);
                    }
            }
            ++label;
        }
    return chunks_.emplace(key, std::move(result)).first->second;
}

int Navigation::component(Cell cell)
{
    if (!inside(cell)) return -1;
    return chunk(chunkAt(cell)).components[(cell.y % chunkSize_) * chunkSize_ + cell.x % chunkSize_];
}

void Navigation::invalidate(Cell cell)
{
    // Diagonal connectivity can depend on a corner in an adjacent chunk.
    const Cell key = chunkAt(cell);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) chunks_.erase({key.x + x, key.y + y});
}

std::vector<Cell> Navigation::path(Cell start, Cell goal, int maxVisited, int maxChunks)
{
    if (!inside(start) || !inside(goal) || maxVisited <= 0 || maxChunks <= 0) return {};
    if (start == goal) return {start};
    const int startComponent = component(start), goalComponent = component(goal);
    if (startComponent < 0 || goalComponent < 0) return {};
    using Key = std::tuple<int, int, int>;
    auto keyFor = [&](Cell c) { const Cell k = chunkAt(c); return Key{k.x, k.y, component(c)}; };
    const Key source = keyFor(start), destination = keyFor(goal);
    struct Entry
    {
        float priority;
        int cost;
        Key key;
        bool operator>(const Entry& other) const { return priority > other.priority; }
    };
    auto estimate = [&](const Key& k) {
        return static_cast<float>(std::max(std::abs(std::get<0>(k) - std::get<0>(destination)), std::abs(std::get<1>(k) - std::get<1>(destination))));
    };
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    std::map<Key, int> costs{{source, 0}};
    std::map<Key, Key> previous;
    open.push({estimate(source), 0, source});
    std::vector<Cell> neighbours;
    int visited = 0;
    bool found = false;
    while (!open.empty() && visited < maxChunks)
    {
        const Entry entry = open.top(); open.pop();
        if (entry.cost != costs[entry.key]) continue;
        if (entry.key == destination) { found = true; break; }
        ++visited;
        const Cell key{std::get<0>(entry.key), std::get<1>(entry.key)};
        const int label = std::get<2>(entry.key);
        const Chunk& here = chunk(key);
        // Only boundary cells have edges into other chunks.
        std::set<Key> exits;
        for (int y = 0; y < chunkSize_; ++y)
            for (int x = 0; x < chunkSize_; ++x)
            {
                if (x != 0 && y != 0 && x != chunkSize_ - 1 && y != chunkSize_ - 1) continue;
                if (here.components[y * chunkSize_ + x] != label) continue;
                const Cell at{key.x * chunkSize_ + x, key.y * chunkSize_ + y};
                grid_.neighbours(at, neighbours);
                for (Cell next : neighbours)
                    if (canStep(at, next) && chunkAt(next) != key && component(next) >= 0) exits.insert(keyFor(next));
            }
        for (const Key& next : exits)
        {
            const int cost = entry.cost + 1;
            if (costs.contains(next) && costs[next] <= cost) continue;
            costs[next] = cost;
            previous[next] = entry.key;
            open.push({cost + estimate(next), cost, next});
        }
    }
    if (!found) return {};
    std::set<Key> corridor{destination};
    for (Key at = destination; at != source;) { at = previous.at(at); corridor.insert(at); }
    std::set<std::pair<int, int>> corridorChunks;
    for (const Key& k : corridor)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) corridorChunks.insert({std::get<0>(k) + x, std::get<1>(k) + y});
    // The one-chunk margin includes the side cells supporting diagonal corner checks.
    return findPath(grid_, start, goal, [&](Cell c) {
        const Cell k = chunkAt(c);
        return inside(c) && passable_(c) && corridorChunks.contains({k.x, k.y});
    }, maxVisited);
}

}
