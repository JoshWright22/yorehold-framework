#pragma once

#include "yorehold/framework/rpg/Stats.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

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

// A named state like Poisoned or Blessed, and what it does to stats while it lasts.
struct ConditionDefinition
{
    std::string id;
    std::string name;
    std::string description;
    std::vector<Modifier> modifiers;
    bool disadvantageOnAttacks = false;
    bool advantageOnAttacks = false;
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
    int baseArmorClass = 10;
    std::string armorClassAbility = "dex"; // added to AC (empty = none)
    std::string initiativeAbility = "dex";
    // Index = level - 1. Empty means no proficiency bonus (classic games).
    std::vector<int> proficiencyByLevel;
    // Total XP needed to reach each level; index 0 = level 2.
    std::vector<int> xpForLevel;
    // A combat turn: free movement up to speed, plus this many actions.
    int actionsPerTurn = 1;
    bool bonusActions = true;       // a bonus action each turn as well
    bool strikeCostsHands = false;  // a Strike costs one action per hand the weapon needs
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
    int levelForXp(int xp) const;
    const AbilityDefinition* ability(std::string_view wanted) const;
    const SkillDefinition* skill(std::string_view wanted) const;
    const ConditionDefinition* condition(std::string_view wanted) const;
    const RestDefinition* rest(std::string_view wanted) const;
    int hitDie(std::string_view characterClass) const;
    std::string toJson() const;
    static std::optional<Ruleset> fromJson(std::string_view json, std::string* error = nullptr);

    // Built-in starting points. Both are written from scratch (game mechanics only, no copied text).
    static Ruleset classic(); // old-school: six abilities, classic modifier table, no skills or proficiency
    static Ruleset modern();  // d20-modern style: skills, proficiency bonus, advantage
};

}
