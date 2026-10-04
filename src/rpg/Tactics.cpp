#include "yorehold/framework/rpg/Tactics.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace yh
{

namespace
{

using nlohmann::json;

struct Preset
{
    const char* name;
    AiProfile profile;
};

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> all = [] {
        std::vector<Preset> list;
        // Walks at the nearest thing and hits it. Never afraid.
        AiProfile mindless;
        mindless.base = "mindless";
        mindless.nearby = 2;
        mindless.random = 0.5f;
        list.push_back({"mindless", mindless});

        // Hunts the hurt and the alone, with the pack. Runs when badly hurt.
        AiProfile animal;
        animal.base = "animal";
        animal.finish = 1;
        animal.weak = 1.5f;
        animal.isolated = 1;
        animal.pack = 1;
        animal.nearby = 0.5f;
        animal.danger = 0.2f;
        animal.random = 0.6f;
        animal.fleeHp = 0.35f;
        list.push_back({"animal", animal});

        // Gangs up, avoids standing where it will be surrounded, and breaks when things go badly.
        AiProfile cunning;
        cunning.base = "cunning";
        cunning.finish = 1.5f;
        cunning.weak = 0.8f;
        cunning.isolated = 0.5f;
        cunning.pack = 1;
        cunning.nearby = 0.3f;
        cunning.danger = 0.5f;
        cunning.random = 0.3f;
        cunning.fleeHp = 0.3f;
        cunning.fleeLosses = 0.75f;
        cunning.fleeLeaderless = true;
        list.push_back({"cunning", cunning});

        // Focuses fire, finishes the wounded and rarely slips up. Holds until nearly dead.
        AiProfile tactical;
        tactical.base = "tactical";
        tactical.finish = 2.5f;
        tactical.weak = 1.2f;
        tactical.isolated = 0.5f;
        tactical.pack = 1.5f;
        tactical.nearby = 0.2f;
        tactical.danger = 0.8f;
        tactical.random = 0.05f;
        tactical.fleeHp = 0.15f;
        list.push_back({"tactical", tactical});
        return list;
    }();
    return all;
}

float hitChance(const TacticalUnit& attacker, const TacticalUnit& target)
{
    // A d20 plus the bonus against AC; a 1 always misses and a 20 always hits.
    return std::clamp(static_cast<float>(21 + attacker.attackBonus - target.armorClass) / 20.0f, 0.05f, 0.95f);
}

bool beside(const Grid& grid, Cell a, Cell b)
{
    return grid.distance(a, b) <= 1.01f;
}

float costAt(const CellCosts& costs, Cell cell, float missing)
{
    const auto found = costs.find(cell);
    return found == costs.end() ? missing : found->second;
}

}

const AiProfile* AiProfile::preset(std::string_view name)
{
    for (const Preset& p : presets())
        if (name == p.name)
            return &p.profile;
    return nullptr;
}

std::optional<AiProfile> AiProfile::fromJson(std::string_view text, std::string* error, const Lookup& lookup, const AiProfile* current)
{
    if (error) error->clear();
    try
    {
        const json j = json::parse(text);
        auto named = [&](const std::string& name) {
            const AiProfile* found = lookup ? lookup(name) : preset(name);
            if (!found) throw std::invalid_argument("Unknown AI \"" + name + "\"");
            return *found;
        };
        AiProfile p;
        if (j.is_string())
            return named(j.get<std::string>());
        if (!j.is_object()) throw std::invalid_argument("AI is a name or an object");
        if (!j.contains("base"))
            p = current ? *current : named("cunning");
        else if (const std::string base = j.at("base").get<std::string>(); base == "none")
            p.base = "custom";
        else
        {
            p = named(base);
            p.base = base;
        }
        p.base = j.value("label", p.base);
        p.damage = j.value("damage", p.damage);
        p.finish = j.value("finish", p.finish);
        p.weak = j.value("weak", p.weak);
        p.isolated = j.value("isolated", p.isolated);
        p.pack = j.value("pack", p.pack);
        p.nearby = j.value("nearby", p.nearby);
        p.danger = j.value("danger", p.danger);
        p.random = j.value("random", p.random);
        p.fleeHp = j.value("fleeHp", p.fleeHp);
        p.fleeLosses = j.value("fleeLosses", p.fleeLosses);
        p.fleeLeaderless = j.value("fleeLeaderless", p.fleeLeaderless);
        p.leader = j.value("leader", p.leader);
        p.escapeAt = j.value("escapeAt", p.escapeAt);
        for (const float value : {p.damage, p.finish, p.weak, p.isolated, p.pack, p.nearby, p.danger, p.random, p.fleeHp, p.fleeLosses, p.escapeAt})
            if (!std::isfinite(value) || value < 0 || value > 1000) throw std::invalid_argument("AI numbers are 0 to 1000");
        if (p.escapeAt < 1) throw std::invalid_argument("escapeAt is at least 1");
        if (p.base.empty() || p.base.size() > 64) throw std::invalid_argument("AI labels are 1 to 64 characters");
        return p;
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

std::string AiProfile::toJson() const
{
    return json{{"base", "none"}, {"label", base}, {"damage", damage}, {"finish", finish}, {"weak", weak}, {"isolated", isolated},
        {"pack", pack}, {"nearby", nearby}, {"danger", danger}, {"random", random}, {"fleeHp", fleeHp}, {"fleeLosses", fleeLosses},
        {"fleeLeaderless", fleeLeaderless}, {"leader", leader}, {"escapeAt", escapeAt}}.dump();
}

bool wantsToFlee(const AiProfile& profile, const TacticalView& view)
{
    if (view.fleeing)
        return true;
    const TacticalUnit& me = view.units.at(view.self);
    if (profile.fleeHp > 0 && static_cast<float>(me.hp) <= profile.fleeHp * static_cast<float>(me.maxHp))
        return true;

    int standing = 0;
    bool leader = false;
    for (const TacticalUnit& unit : view.units)
    {
        if (unit.team != me.team)
            continue;
        standing++;
        leader |= unit.leader;
    }
    const int started = std::max(view.sideAtStart, standing);
    if (static_cast<float>(started - standing) >= profile.fleeLosses * static_cast<float>(started))
        return true;
    return profile.fleeLeaderless && view.hadLeader && !leader;
}

TacticalChoice decide(const AiProfile& profile, const TacticalView& view, const Grid& grid, Random& random, std::vector<TacticalChoice>* considered)
{
    const TacticalUnit& me = view.units.at(view.self);
    std::vector<TacticalChoice> options;

    // What standing on a cell is likely to cost before its next turn: foes next to it hit now,
    // foes that can walk up to it might.
    auto danger = [&](Cell cell) {
        float total = 0;
        for (const TacticalUnit& foe : view.units)
        {
            if (foe.team == me.team)
                continue;
            const float distance = grid.distance(cell, foe.at);
            const float share = distance <= 1.01f ? 1.0f : distance <= static_cast<float>(foe.speed) + 1.01f ? 0.5f : 0.0f;
            total += share * hitChance(foe, me) * foe.averageDamage;
        }
        return total;
    };
    const float unreachable = 1e6f;
    const float here = costAt(view.foeDistance, me.at, unreachable);

    if (wantsToFlee(profile, view))
    {
        // As far from every foe as it can get. Nowhere better to go = cornered, so it fights.
        const CellCosts& cells = view.action && !view.dashReach.empty() ? view.dashReach : view.reach;
        TacticalChoice best;
        best.kind = TacticalChoice::Kind::Flee;
        best.cell = me.at;
        best.score = here;
        for (const auto& [cell, cost] : cells)
        {
            const float away = costAt(view.foeDistance, cell, unreachable);
            if (away >= unreachable)
                continue; // sealed off from every foe: not somewhere to run to
            const float score = away - cost * 0.01f;
            if (score > best.score)
            {
                best.cell = cell;
                best.score = score;
                best.dash = !view.reach.contains(cell);
            }
        }
        if (best.cell != me.at && best.score >= here + 1)
        {
            if (considered) considered->push_back(best);
            return best;
        }
    }

    // Stay where it is and do nothing: what everything else has to beat.
    TacticalChoice hold;
    hold.cell = me.at;
    hold.score = -profile.danger * danger(me.at);
    options.push_back(hold);

    if (view.action)
    {
        for (size_t t = 0; t < view.units.size(); t++)
        {
            const TacticalUnit& target = view.units[t];
            if (target.team == me.team)
                continue;
            const float expected = hitChance(me, target) * me.averageDamage;
            int friends = 0, mine = 0;
            for (size_t other = 0; other < view.units.size(); other++)
            {
                if (other == t || other == view.self || !beside(grid, view.units[other].at, target.at))
                    continue;
                (view.units[other].team == target.team ? friends : mine)++;
            }
            const float who = profile.damage * expected
                + profile.finish * (expected >= static_cast<float>(target.hp) ? 4.0f : 0.0f)
                + profile.weak * 3.0f * (1.0f - static_cast<float>(target.hp) / static_cast<float>(std::max(1, target.maxHp)))
                + profile.isolated * (friends == 0 ? 2.0f : 0.0f)
                + profile.pack * 1.5f * static_cast<float>(mine);
            for (const auto& [cell, cost] : view.reach)
            {
                if (!beside(grid, cell, target.at))
                    continue;
                TacticalChoice attack;
                attack.kind = TacticalChoice::Kind::Attack;
                attack.cell = cell;
                attack.target = t;
                // Hitting someone beats walking about, whoever it is.
                attack.score = 5 + who - profile.nearby * cost * 0.5f - profile.danger * danger(cell) - cost * 0.01f;
                options.push_back(attack);
            }
        }
    }

    // Nobody to hit from here: close in, dashing if that gets nearer.
    const bool canAttack = std::any_of(options.begin(), options.end(), [](const TacticalChoice& o) { return o.kind == TacticalChoice::Kind::Attack; });
    if (!canAttack && here < unreachable)
    {
        const CellCosts& cells = view.action && !view.dashReach.empty() ? view.dashReach : view.reach;
        for (const auto& [cell, cost] : cells)
        {
            const float closer = here - costAt(view.foeDistance, cell, unreachable);
            if (closer <= 0)
                continue;
            TacticalChoice advance;
            advance.kind = TacticalChoice::Kind::Advance;
            advance.cell = cell;
            advance.dash = !view.reach.contains(cell);
            advance.score = closer - profile.danger * 0.25f * danger(cell) - cost * 0.01f;
            options.push_back(advance);
        }
    }

    // A fixed order before the noise goes on, so the same seed picks the same option whatever
    // order the cell maps happen to list their cells in.
    std::sort(options.begin(), options.end(), [](const TacticalChoice& a, const TacticalChoice& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        if (a.cell.y != b.cell.y) return a.cell.y < b.cell.y;
        if (a.cell.x != b.cell.x) return a.cell.x < b.cell.x;
        return a.target < b.target;
    });
    if (profile.random > 0)
        for (TacticalChoice& option : options)
            if (option.kind != TacticalChoice::Kind::Hold)
                option.score += profile.random * static_cast<float>(random.range(0, 1000)) / 1000.0f;
    std::stable_sort(options.begin(), options.end(), [](const TacticalChoice& a, const TacticalChoice& b) { return a.score > b.score; });
    const TacticalChoice best = options.front();
    if (considered) *considered = std::move(options);
    return best;
}

}
