#pragma once

#include "yorehold/framework/rpg/Character.h"
#include "yorehold/framework/rpg/Random.h"
#include "yorehold/framework/rpg/Ruleset.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

// What a player picked when a character gained one level: the class the level went into and the
// options taken with it, by kind ("feats", "spells", "skills"...).
struct LevelChoice
{
    std::string classId;
    std::map<std::string, std::vector<std::string>> picks;
};

// A character as the player made it: choices, not results. The sheet (HP, ranks, speed...) is
// rebuilt from these and the ruleset every time (Compendium::build), so a rules change shows up at
// once and a later migration only has to rewrite choices. Live state (HP lost, conditions, what
// is carried) belongs to the sheet, not here.
struct CharacterChoices
{
    // How the scores were reached. Kept so a creation screen can reopen the same method.
    static constexpr const char* methods[] = {"roll", "pointBuy", "array", "fixed"};

    std::string name;
    std::string race;       // an id among the ruleset's races; empty where the ruleset has none
    std::string background; // likewise
    std::string scoreMethod = "fixed";
    std::map<std::string, int> scores; // every ruleset ability, before race or background changes
    std::vector<LevelChoice> levels;   // one per character level; the first is the starting class
    int xp = 0;
    std::string ruleset; // the ruleset id the character was made under
    std::string notes;

    int level() const { return static_cast<int>(levels.size()); }

    // Checks what can be checked without the compendium: a score for each of the ruleset's
    // abilities and nothing else, in range, and at least one level.
    bool check(const Ruleset& rules, std::string* error = nullptr) const;

    std::string toJson() const;
    static std::optional<CharacterChoices> fromJson(std::string_view json, std::string* error = nullptr);
};

// A first-level character of `classId` with rolled scores: 4d6 keep 3 per ability (3d6 for
// classic), in the ruleset's ability order.
CharacterChoices rollChoices(const Ruleset& rules, std::string name, std::string classId, Random& random);

// For sheets saved before choices existed: the scores on the sheet, every level in `classId`.
CharacterChoices choicesFromSheet(const Ruleset& rules, const Character& sheet, std::string classId);

}
