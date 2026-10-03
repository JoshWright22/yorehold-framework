#pragma once

#include "yorehold/framework/map/Objects.h"
#include "yorehold/framework/map/TileMap.h"

#include <functional>
#include <memory>

namespace yh
{

// Subtract the camera's double-precision origin before converting to renderer floats.
// This preserves small movements in regions far from the world's origin.
struct WorldPosition
{
    double x = 0, y = 0;
    Vec2 relativeTo(WorldPosition origin) const { return {static_cast<float>(x - origin.x), static_cast<float>(y - origin.y)}; }
    WorldPosition translated(Vec2 delta) const { return {x + delta.x, y + delta.y}; }
};

struct Region
{
    std::string id;
    std::unique_ptr<TileMap> map;
    Objects objects;
    std::map<std::string, std::string, std::less<>> variables;
    double simulatedSeconds = 0;

    std::string toJson() const;
    static std::unique_ptr<Region> fromJson(std::string_view json, std::string* error = nullptr);
};

// Player membership determines which regions run. Leaving the last player snapshots and
// unloads a region. Returning restores it; two co-op players can keep different regions active.
// All callbacks run on the calling thread. The loader can obtain authored data from FileSystem.
class Regions
{
public:
    using Loader = std::function<std::unique_ptr<Region>(std::string_view id)>;
    using Tick = std::function<void(Region&, double)>;
    void add(std::string id, Loader loader);
    bool enter(int player, std::string_view id, std::string* error = nullptr);
    void leave(int player);
    Region* active(std::string_view id);
    std::optional<std::string> playerRegion(int player) const;
    size_t activeCount() const;
    void update(double dt, const Tick& tick = {});
    std::string toJson() const;
    // Load saved snapshots into registered, currently empty regions. Atomic on parse failure.
    bool restore(std::string_view json, std::string* error = nullptr);
    // Compact saves: edited terrain only, plus complete object/variable/clock state.
    // restoreChanges reloads the authored maps with each registered loader, then applies
    // the edits. Register the same region ids and layer layout before restoring.
    std::string changesJson() const;
    bool restoreChanges(std::string_view json, std::string* error = nullptr);

private:
    struct Slot
    {
        Loader loader;
        std::string saved;
        std::unique_ptr<Region> region;
        std::set<int> players;
    };
    std::map<std::string, Slot, std::less<>> slots_;
    std::map<int, std::string> players_;
    bool updating_ = false;
};

}
