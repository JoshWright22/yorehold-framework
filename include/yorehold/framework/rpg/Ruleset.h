#pragma once

#include "yorehold/framework/rpg/Stats.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

class FileSystem;

struct AbilityDefinition
{
    std::string id;   // "str"
    std::string name; // "Strength"
};

struct SkillDefinition
{
    std::string id;      // "stealth"
    std::string name;    // "Stealth"
    std::string ability; // ability id it uses
};

// A named state like Poisoned or Blessed: what it does to stats while it lasts, what it stops the
// creature doing, how long it lasts and what ends it. One JSON object each, in a ruleset's
// `conditions` list or a file of its own (see Ruleset::loadConditions).
struct ConditionDefinition
{
    // What applying it again does to one already there.
    enum class Stacking
    {
        Refresh, // the new duration replaces the old
        Longest, // whichever lasts longer stays
        Value,   // values add up to `maxValue` (Frightened 1 + 1 = Frightened 2)
    };

    std::string id;
    std::string name;
    std::string description;
    std::vector<Modifier> modifiers;
    bool disadvantageOnAttacks = false;
    bool advantageOnAttacks = false;

    // Named switches the game or the encounter asks about with Character::hasFlag. An encounter
    // honours "cantAct" (no actions or reactions) and "cantMove" (no movement); the rest are the game's.
    std::vector<std::string> flags;
    int duration = -1; // rounds it lasts when applied without one (-1 = until something ends it)
    Stacking stacking = Stacking::Refresh;
    int maxValue = 1;      // highest value it stacks to (Stacking::Value)
    bool perValue = false; // additive modifiers are multiplied by the value
    int decay = 0;         // the value drops by this at the end of each round; at 0 the condition ends
    // Events that end it (see conditionEvents): "damage", "attack", "turnStart", "rest"...
    std::vector<std::string> ends;
    // A save at the end of each round that ends it on a success. No ability = no save.
    std::string saveAbility;
    int saveDc = 10;
    std::vector<std::string> removes; // conditions taken off when this one is applied (Dead removes Dying)

    bool hasFlag(std::string_view flag) const;
    bool endsOn(std::string_view event) const;
    std::string toJson() const;
    static std::optional<ConditionDefinition> fromJson(std::string_view json, std::string* error = nullptr);
};

// The events a condition's `ends` may name. An encounter raises turnStart, turnEnd, attack and
// damage itself; the game raises the others (and may raise any of them) with Character::conditionEvent.
inline constexpr const char* conditionEvents[] = {
    "turnStart", "turnEnd", "attack", "damage", "healed", "move", "rest", "fightStart", "fightEnd",
};

// How much HP a rest or a win gives back. Several styles, so each ruleset picks its own.
struct Recovery
{
    enum class Kind
    {
        None,
        Full,     // back to max HP
        Fraction, // `fraction` of max HP
        Flat,     // `amount` HP
        HitDice,  // `amount` hit dice (0 = one per level), each plus the hit-die ability's modifier
    };
    Kind kind = Kind::None;
    float fraction = 0.5f;
    int amount = 0;
    bool reviveDowned = false; // also heals characters at 0 HP (otherwise only standing ones)
};

struct RestDefinition
{
    std::string id;   // "short"
    std::string name; // "Short rest"
    Recovery recovery;
    int perAdventure = 0; // 0 = unlimited
};

// How score -> modifier works.
enum class ModifierTable
{
    D20,     // (score - 10) / 2, rounded down: 8 -> -1, 14 -> +2
    Classic, // classic table: 3 -> -3, 4-5 -> -2, 6-8 -> -1, 9-12 -> 0, 13-15 -> +1, 16-17 -> +2, 18 -> +3
};

// The game system as data: which abilities, skills and conditions exist and how the numbers work.
// Chapters pick a ruleset, so the framework never hard-codes one game's rules.
struct ProficiencyRankDefinition
{
    std::string id;
    std::string name;
    int bonus = 0;
    bool addsLevel = false;
};

struct DeathRules
{
    bool enabled = false;
    int saveDc = 10;
    int successes = 3;
    int failures = 3;
    int naturalOneFailures = 2;
    int naturalTwentyHp = 1; // 0 = an ordinary successful save
    int damageFailures = 1;
    int criticalDamageFailures = 2;
    std::string downedCondition, dyingCondition, stableCondition, deadCondition;
};

// The ways a new character's ability scores can be set (CharacterChoices::scoreMethod).
struct ScoreMethods
{
    std::string roll = "4d6kh3";                            // rolled once per ability, in order
    std::vector<int> standardArray{15, 14, 13, 12, 10, 8}; // one value per ability, each used once
    int pointBudget = 27;
    // Score -> what it costs. Only these scores can be bought.
    std::map<int, int> pointCosts{{8, 0}, {9, 1}, {10, 2}, {11, 3}, {12, 4}, {13, 5}, {14, 7}, {15, 9}};
};

struct Ruleset
{
    std::string id;
    std::string name;
    std::vector<AbilityDefinition> abilities;
    std::vector<SkillDefinition> skills;
    std::vector<ConditionDefinition> conditions;
    ModifierTable modifierTable = ModifierTable::D20;
    int scoreMin = 3;
    int scoreMax = 20;
    ScoreMethods scoreMethods;
    int baseArmorClass = 10;
    std::string armorClassAbility = "dex"; // added to AC (empty = none)
    std::string initiativeAbility = "dex";
    // Index = level - 1. Empty means no proficiency bonus (classic games).
    std::vector<int> proficiencyByLevel;
    // Empty keeps the older per-level table. Otherwise choices and legacy proficiency lists select a rank.
    std::vector<ProficiencyRankDefinition> proficiencyRanks;
    std::string proficientRank;
    std::string untrainedRank;
    int baseDc = 10;
    DeathRules death;
    // Total XP needed to reach each level; index 0 = level 2.
    std::vector<int> xpForLevel;
    // A combat turn: free movement up to speed, plus this many actions.
    int actionsPerTurn = 1;
    bool bonusActions = true;       // a bonus action each turn as well
    bool strikeCostsHands = false;  // a Strike costs one action per hand the weapon needs
    bool sharedTurns = false;      // consecutive allies may interleave their turns
    int feetPerSquare = 5;
    int carryPerStrength = 15; // pounds per point of the first ability (strength)
    int magicItemLimit = 0;    // magic items one character may carry (0 = no limit)
    int passiveBase = 10;      // a passive score (what a check is rolled against) is this plus the modifier

    // Healing. Rests are offered by the client between fights; afterVictory heals the winners.
    std::vector<RestDefinition> rests;
    Recovery afterVictory;
    int reviveAfterVictory = 0; // downed winners get up with this much HP (0 = they stay down)
    int defaultHitDie = 8;
    std::vector<std::pair<std::string, int>> hitDieByClass; // {"Fighter", 10}
    std::string hitDieAbility = "con";                      // added per hit die (empty = none)

    int abilityModifier(int score) const;
    int proficiencyBonus(int level) const;
    const ProficiencyRankDefinition* proficiencyRank(std::string_view id) const;
    int proficiencyBonus(int level, std::string_view rank) const;
    int levelForXp(int xp) const;
    const AbilityDefinition* ability(std::string_view wanted) const;
    const SkillDefinition* skill(std::string_view wanted) const;
    const ConditionDefinition* condition(std::string_view wanted) const;
    const RestDefinition* rest(std::string_view wanted) const;
    int hitDie(std::string_view characterClass) const;
    bool checkDeathRules(std::string* error = nullptr) const;
    std::string toJson() const;
    static std::optional<Ruleset> fromJson(std::string_view json, std::string* error = nullptr);
    // Adds every `folder`/<id>.json as a condition, replacing any with the same id. The file name
    // is the id. All-or-nothing: on an error nothing changes and `error` names the file.
    bool loadConditions(const FileSystem& files, std::string_view folder, std::string* error = nullptr);

    // Built-in starting points. Both are written from scratch (game mechanics only, no copied text).
    static Ruleset classic(); // old-school: six abilities, classic modifier table, no skills or proficiency
    static Ruleset modern();  // d20-modern style: skills, proficiency bonus, advantage
};

}
