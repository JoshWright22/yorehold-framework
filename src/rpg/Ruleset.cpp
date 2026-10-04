#include "yorehold/framework/rpg/Ruleset.h"

#include <algorithm>
#include <cmath>

namespace yh
{

namespace
{

std::vector<AbilityDefinition> sixAbilities()
{
    return {
        {"str", "Strength"}, {"dex", "Dexterity"}, {"con", "Constitution"},
        {"int", "Intelligence"}, {"wis", "Wisdom"}, {"cha", "Charisma"},
    };
}

std::vector<ConditionDefinition> commonConditions()
{
    ConditionDefinition poisoned{"poisoned", "Poisoned", "Sick and slow to react.", {}, true, false};
    ConditionDefinition blessed{"blessed", "Blessed", "Fortune favours you.", {{"attack", Modifier::Op::Add, 2, ""}}, false, false};
    ConditionDefinition slowed{"slowed", "Slowed", "Moves at half speed.", {{"speed", Modifier::Op::Multiply, 0.5f, ""}}, false, false};
    ConditionDefinition shielded{"shielded", "Shielded", "A magical barrier turns blows aside.", {{"ac", Modifier::Op::Add, 4, ""}}, false, false};
    ConditionDefinition inspired{"inspired", "Inspired", "Ready for anything.", {}, false, true};
    return {poisoned, blessed, slowed, shielded, inspired};
}

}

int Ruleset::abilityModifier(int score) const
{
    if (modifierTable == ModifierTable::Classic)
    {
        if (score <= 3) return -3;
        if (score <= 5) return -2;
        if (score <= 8) return -1;
        if (score <= 12) return 0;
        if (score <= 15) return 1;
        if (score <= 17) return 2;
        return 3;
    }
    return static_cast<int>(std::floor((score - 10) / 2.0));
}

const ProficiencyRankDefinition* Ruleset::proficiencyRank(std::string_view wanted) const
{
    for (const auto& rank : proficiencyRanks)
        if (rank.id == wanted) return &rank;
    return nullptr;
}

int Ruleset::proficiencyBonus(int level, std::string_view wanted) const
{
    const auto* rank = proficiencyRank(wanted);
    return rank ? rank->bonus + (rank->addsLevel ? std::max(0, level) : 0) : 0;
}

int Ruleset::proficiencyBonus(int level) const
{
    if (proficiencyByLevel.empty())
        return 0;
    return proficiencyByLevel[std::clamp(level, 1, static_cast<int>(proficiencyByLevel.size())) - 1];
}

bool Ruleset::checkDeathRules(std::string* error) const
{
    if (error) error->clear();
    if (!death.enabled) return true;
    for (const auto* id_ : {&death.downedCondition, &death.dyingCondition, &death.stableCondition, &death.deadCondition})
        if (!id_->empty() && !condition(*id_))
        {
            if (error) *error = "Unknown death condition: " + *id_;
            return false;
        }
    return true;
}

int Ruleset::levelForXp(int xp) const
{
    int level = 1;
    for (const int needed : xpForLevel)
    {
        if (xp < needed)
            break;
        level++;
    }
    return level;
}

const AbilityDefinition* Ruleset::ability(std::string_view wanted) const
{
    for (const AbilityDefinition& a : abilities)
    {
        if (a.id == wanted)
            return &a;
    }
    return nullptr;
}

const SkillDefinition* Ruleset::skill(std::string_view wanted) const
{
    for (const SkillDefinition& s : skills)
    {
        if (s.id == wanted)
            return &s;
    }
    return nullptr;
}

const ConditionDefinition* Ruleset::condition(std::string_view wanted) const
{
    for (const ConditionDefinition& c : conditions)
    {
        if (c.id == wanted)
            return &c;
    }
    return nullptr;
}

const RestDefinition* Ruleset::rest(std::string_view wanted) const
{
    for (const RestDefinition& r : rests)
    {
        if (r.id == wanted)
            return &r;
    }
    return nullptr;
}

const SurfaceDefinition* Ruleset::surface(std::string_view wanted) const
{
    for (const SurfaceDefinition& s : surfaces)
    {
        if (s.id == wanted)
            return &s;
    }
    return nullptr;
}

int Ruleset::hitDie(std::string_view characterClass) const
{
    for (const auto& [className, sides] : hitDieByClass)
    {
        if (className == characterClass)
            return sides;
    }
    return defaultHitDie;
}

Ruleset Ruleset::classic()
{
    Ruleset r;
    r.id = "classic";
    r.name = "Classic";
    r.abilities = sixAbilities();
    r.conditions = commonConditions();
    r.modifierTable = ModifierTable::Classic;
    r.scoreMax = 18;
    r.scoreMethods.roll = "3d6";
    r.xpForLevel = {1500, 3500, 7500, 15000, 30000, 60000, 110000, 220000, 330000}; // placeholder curve, tune later
    // Old-school: slow natural healing, one hit die per rest, nobody gets back up by themselves.
    r.rests = {{"rest", "Rest", {Recovery::Kind::HitDice, 0.5f, 1, false}, 0}};
    r.hitDieAbility.clear();
    r.hitDieByClass = {{"Fighter", 8}, {"Cleric", 6}, {"Thief", 4}, {"Magic-user", 4}};
    r.defaultHitDie = 6;
    return r;
}

Ruleset Ruleset::modern()
{
    Ruleset r;
    r.id = "modern";
    r.name = "Modern d20";
    r.abilities = sixAbilities();
    r.skills = {
        {"athletics", "Athletics", "str"}, {"acrobatics", "Acrobatics", "dex"}, {"stealth", "Stealth", "dex"},
        {"arcana", "Arcana", "int"}, {"investigation", "Investigation", "int"}, {"perception", "Perception", "wis"},
        {"insight", "Insight", "wis"}, {"survival", "Survival", "wis"}, {"persuasion", "Persuasion", "cha"},
        {"deception", "Deception", "cha"},
    };
    r.conditions = commonConditions();
    r.modifierTable = ModifierTable::D20;
    r.proficiencyByLevel = {2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6};
    r.xpForLevel = {250, 800, 2500, 6000, 13000, 22000, 33000, 46000, 62000}; // placeholder curve, tune later
    // Short rests spend hit dice; a long rest heals everyone fully, even the downed.
    r.rests = {
        {"short", "Short rest", {Recovery::Kind::HitDice, 0.5f, 0, false}, 2},
        {"long", "Long rest", {Recovery::Kind::Full, 1.0f, 0, true}, 1},
    };
    r.reviveAfterVictory = 1;
    r.hitDieByClass = {{"Barbarian", 12}, {"Fighter", 10}, {"Cleric", 8}, {"Rogue", 8}, {"Wizard", 6}};
    // Two actions a turn, no bonus action; a two-handed weapon takes both to swing.
    r.actionsPerTurn = 2;
    r.bonusActions = false;
    r.strikeCostsHands = true;
    return r;
}

}
