#pragma once

#include "yorehold/framework/map/Grid.h"
#include "yorehold/framework/rpg/Random.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace yh
{

// How a creature picks what to do on its turn. Every option it has is scored with these weights
// and the best one wins, so a new kind of creature is a new set of numbers, not new code.
//
// Wherever an AI is written in data it is either a name ("cunning", or any profile the game has
// loaded) or an object of changes: {"fleeHp": 0.5} adjusts whatever AI was there before,
// {"base": "animal", "pack": 3} starts again from a named one, {"base": "none", ...} from nothing.
struct AiProfile
{
    std::string base = "cunning"; // the profile the numbers started from (shown in debug notes)

    // Choosing who to hit and where to stand.
    float damage = 1;    // damage it expects to deal
    float finish = 0;    // a blow likely to drop the target
    float weak = 0;      // targets that are already hurt
    float isolated = 0;  // targets with none of their friends next to them
    float pack = 0;      // targets its own allies are already on
    float nearby = 0;    // targets that take less walking
    float danger = 0;    // damage it expects to take where it ends up
    float random = 0;    // noise on every score: higher makes more mistakes

    // Morale: when it stops fighting and runs. A cornered creature fights on.
    float fleeHp = 0;            // at or below this share of its HP (0 = never)
    float fleeLosses = 2;        // once this share of its side is down (above 1 = never)
    bool fleeLeaderless = false; // once its side had a leader and none is left standing
    bool leader = false;         // counts as a leader for its allies
    float escapeAt = 8;          // squares of walking from the nearest foe at which it can get away

    // Built in: "mindless", "animal", "cunning", "tactical". Null for anything else.
    static const AiProfile* preset(std::string_view name);
    // Finds a profile by name; games pass one that also knows the profiles in their data files.
    using Lookup = std::function<const AiProfile*(std::string_view)>;
    // `current` is the AI being adjusted when the object names no base (none: "cunning").
    static std::optional<AiProfile> fromJson(std::string_view json, std::string* error = nullptr, const Lookup& lookup = {},
        const AiProfile* current = nullptr);
    // Every number, so it reads back the same whatever profiles are loaded.
    std::string toJson() const;
};

// One creature standing in the fight, as the scoring sees it.
struct TacticalUnit
{
    int team = 0;
    Cell at;
    int hp = 1;
    int maxHp = 1;
    int armorClass = 10;
    int attackBonus = 0;
    float averageDamage = 1;
    int speed = 6; // squares per turn
    bool leader = false;
};

using CellCosts = std::unordered_map<Cell, float, CellHash>;

struct TacticalView
{
    std::vector<TacticalUnit> units; // everyone still standing, both sides
    size_t self = 0;
    bool action = true;    // it can still attack or dash this turn
    CellCosts reach;       // squares it can end on with the movement it has (the one it stands on costs 0)
    CellCosts dashReach;   // ... and with a dash on top; empty = can't dash
    CellCosts foeDistance; // squares of walking from each cell to the nearest foe
    int sideAtStart = 1;   // how many its side began the fight with
    bool hadLeader = false;
    bool fleeing = false;  // it already broke on an earlier turn: it keeps running
};

struct TacticalChoice
{
    enum class Kind { Hold, Attack, Advance, Flee };
    Kind kind = Kind::Hold;
    Cell cell;         // where it ends its move (its own cell = stays put)
    size_t target = 0; // index into units, for Attack
    bool dash = false; // the move needs a dash
    float score = 0;
};

// Its morale has broken (see the flee* numbers).
bool wantsToFlee(const AiProfile& profile, const TacticalView& view);
// Scores every option and returns the best. `considered` gets all of them, best first (for debug views).
TacticalChoice decide(const AiProfile& profile, const TacticalView& view, const Grid& grid, Random& random,
    std::vector<TacticalChoice>* considered = nullptr);

}
