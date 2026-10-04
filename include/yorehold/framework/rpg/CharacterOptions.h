#pragma once

#include "yorehold/framework/rpg/Character.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

// A playable people: what being one of them adds to a sheet. Scores change by `abilities`, speed
// replaces the class's when set, darkvision is the better of the two, and its feats come free.
struct RaceDefinition
{
    std::string id;   // "dwarf"
    std::string name; // "Dwarf"
    std::string description;
    int speed = 0;      // feet; 0 = the class's
    int darkvision = 0; // feet
    int bonusHp = 0;    // added to first-level HP
    std::map<std::string, int> abilities; // "con": 2
    std::set<std::string> proficiencies;
    std::vector<std::string> feats; // feat ids every member has
};

// Where a character comes from: a few trained skills, a feat and some gear.
struct BackgroundDefinition
{
    std::string id;
    std::string name;
    std::string description;
    std::map<std::string, int> abilities;
    std::set<std::string> proficiencies;
    std::vector<std::string> feats;
    std::vector<std::string> items; // item ids, after the class's
};

// Something a character learned, taken at the levels a class allows (see the class's level
// table) or given by a race or background. What it does to the sheet is plain data; anything that
// acts in play grows from the effects vocabulary.
struct FeatDefinition
{
    // The kinds a level can offer. A feat is one of them.
    static constexpr const char* kinds[] = {"class", "skill", "general", "race"};

    std::string id;
    std::string name;
    std::string description;
    std::string kind = "general";
    bool repeatable = false;

    // Needed to pick it (JSON: "requires"). Empty lists don't restrict.
    struct Requirements
    {
        int level = 1;
        std::vector<std::string> races;
        std::vector<std::string> classes;       // a level in any of these
        std::map<std::string, int> abilities;   // minimum scores
        std::vector<std::string> proficiencies; // trained (or better) already
    };
    Requirements needs;

    // What it gives.
    std::vector<Modifier> modifiers;      // on the sheet for good: "maxHp", "speed", "ac", "attack"...
    std::set<std::string> proficiencies;  // trained in these
    std::map<std::string, std::string> ranks;  // raised to at least this rank
    std::map<std::string, Resource> resources; // added to these maximums (started if new)
};

// The player options a ruleset offers, read from its folder: races/, backgrounds/ and feats/,
// one JSON file each named after its id. Unknown fields are refused with the field named.
std::optional<RaceDefinition> raceFromJson(std::string_view json, std::string* error = nullptr);
std::optional<BackgroundDefinition> backgroundFromJson(std::string_view json, std::string* error = nullptr);
std::optional<FeatDefinition> featFromJson(std::string_view json, std::string* error = nullptr);

}
