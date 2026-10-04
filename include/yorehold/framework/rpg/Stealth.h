#pragma once

#include "yorehold/framework/graphics/Lighting.h"
#include "yorehold/framework/map/LightLevels.h"
#include "yorehold/framework/rpg/Random.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

// The numbers stealth runs on. They belong to the ruleset, so a vote can change them.
// Distances are in world units; the game converts from metres.
struct StealthRules
{
    float checkEvery = 5;      // distance a sneaker moves inside a cone between Stealth checks
    float sneakSpeed = 0.5f;   // movement multiplier while sneaking
    int darkBonus = 5;         // added to Stealth checks made in darkness
    int dimBonus = 2;          // ... in dim light
    int brightBonus = 0;       // ... in bright light
    bool critical = true;      // a natural 1 is always spotted, a natural 20 never

    int lightBonus(LightLevel level) const;
    static std::optional<StealthRules> fromJson(std::string_view json, std::string* error = nullptr);
    std::string toJson() const;
};

// Someone keeping watch. They see in a cone in front of them, out to `range`; in the dark only out
// to `darkRange` (darkvision; 0 = can't see into darkness at all).
struct Watcher
{
    Vec2 position;
    float facing = 0;       // radians, 0 = +x
    float coneAngle = 2.1f; // full width of the cone in radians (about 120 degrees)
    float range = 400;
    float darkRange = 0;
    int passivePerception = 10;
    bool alert = false;     // searching: the cone becomes a full circle
};

// Can this watcher see a point? `light` says how lit it is (null: everything is bright).
bool sees(const Watcher& watcher, Vec2 point, std::span<const Wall> walls,
    const std::function<LightLevel(Vec2)>& light = {});

// The outline of a watcher's cone, cut short by walls, for drawing while sneaking. Closed polygon
// starting at the watcher.
std::vector<Vec2> visionCone(const Watcher& watcher, std::span<const Wall> walls, int segments = 24);

struct StealthCheck
{
    size_t watcher = 0;
    Vec2 at;              // where the check happened
    int roll = 0;         // the d20
    int total = 0;        // roll + Stealth bonus + light bonus
    int dc = 0;           // the watcher's passive Perception
    bool spotted = false;
};

// Follows one sneaking creature past a group of watchers. Feed it each stretch of movement; it
// makes a Stealth check when the sneaker first comes into a watcher's view, then again every
// `checkEvery` of movement in view. Someone not sneaking is spotted as soon as they're seen.
class StealthTracker
{
public:
    explicit StealthTracker(StealthRules rules = {}) : rules_(rules) {}

    const StealthRules& rules() const { return rules_; }
    // Forget progress toward the next check (a new turn, a new area, or out of sight for a while).
    void reset() { travelled_.clear(); }

    // Moves the sneaker from `from` to `to` in a straight line. Returns every check made, in
    // order, stopping at the first one that spots them. `stealthBonus` is their Stealth modifier.
    std::vector<StealthCheck> move(Vec2 from, Vec2 to, bool sneaking, int stealthBonus,
        std::span<const Watcher> watchers, std::span<const Wall> walls, Random& random,
        const std::function<LightLevel(Vec2)>& light = {});

private:
    StealthRules rules_;
    std::vector<float> travelled_; // per watcher: movement in view since the last check; < 0 = not in view
};

}
