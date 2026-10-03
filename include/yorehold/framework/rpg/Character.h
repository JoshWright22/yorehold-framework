#pragma once

#include "yorehold/framework/rpg/Dice.h"
#include "yorehold/framework/rpg/Ruleset.h"
#include "yorehold/framework/rpg/Stats.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace yh
{

struct Item
{
    std::string id;
    std::string name;
    std::string slot;   // where it's worn/held: "mainHand", "offHand", "armor", "head", "ring"... empty = can't equip
    std::string damage; // dice, for weapons ("1d8")
    std::string attackAbility = "str"; // finesse/ranged weapons use "dex"
    float weight = 0;   // pounds
    int value = 0;      // copper
    int quantity = 1;
    bool equipped = false;
    std::vector<Modifier> modifiers; // apply while equipped
};

// Anything with a current/max that gets spent: spell slots, rage uses, arrows...
struct Resource
{
    int current = 0;
    int max = 0;
};

struct ActiveCondition
{
    std::string id;
    int roundsLeft = -1; // -1 = until removed
};

// A character sheet. Holds raw data; anything derived (modifiers, AC, carry limit) is computed
// from it and a Ruleset, so switching rulesets never leaves stale numbers behind.
class Character
{
public:
    std::string name;
    std::string ancestry;
    std::string characterClass;
    int level = 1;
    int xp = 0;
    std::string hitDie = "1d8";

    StatBlock stats; // abilities by id ("str"...), plus "maxHp", "ac", "speed" (feet), "attack", "damage"
    int hp = 0;
    int tempHp = 0;
    std::map<std::string, Resource> resources;
    std::vector<Item> inventory;
    std::vector<ActiveCondition> conditions;
    std::set<std::string> proficiencies; // skill ids, ability ids (saves), "weapons", "armor"
    std::string notes;

    int abilityScore(std::string_view ability) const { return stats.integer(ability); }
    int abilityModifier(const Ruleset& rules, std::string_view ability) const;
    int maxHp() const { return stats.integer("maxHp"); }
    int armorClass(const Ruleset& rules) const;
    int speedFeet() const { return stats.integer("speed"); }
    int speedSquares(const Ruleset& rules) const { return speedFeet() / std::max(1, rules.feetPerSquare); }

    // Ability or skill check: d20 + modifier (+ proficiency if proficient).
    int checkModifier(const Ruleset& rules, std::string_view abilityOrSkill) const;
    int saveModifier(const Ruleset& rules, std::string_view ability) const;
    int initiativeModifier(const Ruleset& rules) const;
    RollResult rollCheck(const Ruleset& rules, std::string_view abilityOrSkill, Advantage advantage, Random& random) const;
    RollResult rollSave(const Ruleset& rules, std::string_view ability, Advantage advantage, Random& random) const;

    // Weapon attacks: the equipped main-hand item, or an unarmed strike.
    const Item* weapon() const;
    int attackModifier(const Ruleset& rules) const;
    std::string damageDice(const Ruleset& rules) const;
    // Conditions can force advantage/disadvantage on attacks.
    Advantage attackAdvantage(const Ruleset& rules) const;

    // Damage burns temporary HP first. Returns true if this dropped the character to 0.
    bool takeDamage(int amount);
    void heal(int amount);
    bool down() const { return hp <= 0; }

    bool equip(size_t inventoryIndex);
    void unequip(size_t inventoryIndex);

    void addCondition(const Ruleset& rules, std::string_view id, int rounds = -1);
    void removeCondition(std::string_view id);
    bool hasCondition(std::string_view id) const;
    // Counts down timed conditions; call once per round.
    void endRound();

    float carriedWeight() const;
    float carryCapacity(const Ruleset& rules) const;

    void addXp(const Ruleset& rules, int amount); // levels up automatically

    // Save files. Returns false and fills `error` if the JSON is broken.
    std::string toJson() const;
    static std::optional<Character> fromJson(std::string_view json, std::string* error = nullptr);
};

// Rolls a new character: 4d6 keep 3 per ability (or 3d6 for classic), HP = max hit die + CON.
Character makeRandomCharacter(const Ruleset& rules, std::string name, std::string characterClass, Random& random);

}
