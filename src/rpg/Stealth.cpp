#include "yorehold/framework/rpg/Stealth.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace yh
{

namespace
{

using nlohmann::json;

float length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

// The angle between two directions, 0 to pi.
float angleBetween(float a, float b)
{
    const float twoPi = 2 * std::numbers::pi_v<float>;
    float d = std::fmod(std::fabs(a - b), twoPi);
    return d > std::numbers::pi_v<float> ? twoPi - d : d;
}

// How far along the ray from `origin` in `direction` (unit) it hits the wall, or `limit`.
float rayTo(Vec2 origin, Vec2 direction, float limit, std::span<const Wall> walls)
{
    float best = limit;
    for (const Wall& wall : walls)
    {
        const Vec2 edge = wall.b - wall.a;
        const float denominator = direction.x * edge.y - direction.y * edge.x;
        if (std::fabs(denominator) < 1e-6f) continue;
        const Vec2 toWall = wall.a - origin;
        const float along = (toWall.x * edge.y - toWall.y * edge.x) / denominator;
        const float onWall = (toWall.x * direction.y - toWall.y * direction.x) / denominator;
        if (along >= 0 && onWall >= 0 && onWall <= 1) best = std::min(best, along);
    }
    return best;
}

}

int StealthRules::lightBonus(LightLevel level) const
{
    switch (level)
    {
    case LightLevel::Dark: return darkBonus;
    case LightLevel::Dim: return dimBonus;
    default: return brightBonus;
    }
}

std::optional<StealthRules> StealthRules::fromJson(std::string_view text, std::string* error)
{
    if (error) error->clear();
    try
    {
        const json j = json::parse(text);
        if (!j.is_object()) throw std::invalid_argument("stealth rules are an object");
        StealthRules r;
        r.checkEvery = j.value("checkEvery", r.checkEvery);
        r.sneakSpeed = j.value("sneakSpeed", r.sneakSpeed);
        r.darkBonus = j.value("darkBonus", r.darkBonus);
        r.dimBonus = j.value("dimBonus", r.dimBonus);
        r.brightBonus = j.value("brightBonus", r.brightBonus);
        r.critical = j.value("critical", r.critical);
        if (!std::isfinite(r.checkEvery) || r.checkEvery <= 0 || r.checkEvery > 10000)
            throw std::invalid_argument("checkEvery is above 0 and at most 10000");
        if (!std::isfinite(r.sneakSpeed) || r.sneakSpeed <= 0 || r.sneakSpeed > 1)
            throw std::invalid_argument("sneakSpeed is above 0 and at most 1");
        for (const int bonus : {r.darkBonus, r.dimBonus, r.brightBonus})
            if (bonus < -20 || bonus > 20) throw std::invalid_argument("light bonuses are -20 to 20");
        return r;
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

std::string StealthRules::toJson() const
{
    return json{{"checkEvery", checkEvery}, {"sneakSpeed", sneakSpeed}, {"darkBonus", darkBonus}, {"dimBonus", dimBonus},
        {"brightBonus", brightBonus}, {"critical", critical}}.dump();
}

bool sees(const Watcher& watcher, Vec2 point, std::span<const Wall> walls, const std::function<LightLevel(Vec2)>& light)
{
    const Vec2 delta = point - watcher.position;
    const float distance = length(delta);
    if (distance > watcher.range) return false;
    if (!watcher.alert && distance > 1e-4f && angleBetween(std::atan2(delta.y, delta.x), watcher.facing) > watcher.coneAngle / 2)
        return false;
    if (light && distance > watcher.darkRange && light(point) == LightLevel::Dark) return false;
    return lineOfSight(watcher.position, point, walls);
}

std::vector<Vec2> visionCone(const Watcher& watcher, std::span<const Wall> walls, int segments)
{
    segments = std::max(segments, 2);
    const float width = watcher.alert ? 2 * std::numbers::pi_v<float> : std::clamp(watcher.coneAngle, 0.0f, 2 * std::numbers::pi_v<float>);
    std::vector<Vec2> outline{watcher.position};
    for (int i = 0; i <= segments; ++i)
    {
        const float angle = watcher.facing - width / 2 + width * i / segments;
        const Vec2 direction{std::cos(angle), std::sin(angle)};
        outline.push_back(watcher.position + direction * rayTo(watcher.position, direction, watcher.range, walls));
    }
    return outline;
}

std::vector<StealthCheck> StealthTracker::move(Vec2 from, Vec2 to, bool sneaking, int stealthBonus,
    std::span<const Watcher> watchers, std::span<const Wall> walls, Random& random, const std::function<LightLevel(Vec2)>& light)
{
    travelled_.resize(watchers.size(), -1);
    std::vector<StealthCheck> checks;
    const float total = length(to - from);
    // Small steps so a check lands close to where the distance runs out.
    const float step = std::max(rules_.checkEvery / 8, 1e-3f);
    const int steps = static_cast<int>(std::ceil(total / step));
    for (int s = 0; s <= steps; ++s)
    {
        const Vec2 at = steps ? from + (to - from) * (static_cast<float>(s) / steps) : from;
        const float moved = s ? total / steps : 0;
        for (size_t i = 0; i < watchers.size(); ++i)
        {
            if (!sees(watchers[i], at, walls, light))
            {
                travelled_[i] = -1;
                continue;
            }
            bool check = travelled_[i] < 0;
            travelled_[i] = check ? 0 : travelled_[i] + moved;
            if (travelled_[i] >= rules_.checkEvery)
            {
                travelled_[i] -= rules_.checkEvery;
                check = true;
            }
            if (!check && sneaking) continue;
            StealthCheck c{i, at};
            c.dc = watchers[i].passivePerception;
            if (sneaking)
            {
                c.roll = random.range(1, 20);
                c.total = c.roll + stealthBonus + rules_.lightBonus(light ? light(at) : LightLevel::Bright);
                c.spotted = c.total < c.dc;
                if (rules_.critical && c.roll == 1) c.spotted = true;
                if (rules_.critical && c.roll == 20) c.spotted = false;
            }
            else
                c.spotted = true;
            checks.push_back(c);
            if (c.spotted) return checks;
        }
    }
    return checks;
}

}
