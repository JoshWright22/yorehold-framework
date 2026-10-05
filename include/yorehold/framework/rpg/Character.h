#pragma once

#include "yorehold/framework/rpg/Dice.h"
#include "yorehold/framework/rpg/Ruleset.h"
#include "yorehold/framework/rpg/Stats.h"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace yh
{

struct ActionDefinition;

struct Item
{
    std::string id;
    std::string name;
    std::string slot;   // where it's worn/held: "mainHand", "offHand", "armor", "head", "ring"... empty = can't equip
    std::string damage; // dice, for weapons ("1d8")
    std::string attackAbility = "str"; // finesse/ranged weapons use "dex"
    int hands = 1;      // hands needed to use it (a greatsword or bow: 2)
    float weight = 0;   // pounds
    int value = 0;      // copper
    int quantity = 1;
    bool magic = false; // counts toward the ruleset's magic item limit
    bool equipped = false;
    std::vector<Modifier> modifiers; // apply while equipped
    std::shared_ptr<const ActionDefinition> use; // optional consumable action, preserved with the item
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
    int value = 1;       // how strongly, for conditions that stack by value (Frightened 2)
};

struct DeathState
{
    bool saves = true; // creatures can opt out and die immediately at zero HP
    int successes = 0;
    int failures = 0;
    bool stable = false;
    bool dead = false;
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
    std::vector<std::string> spells; // spell ids it can cast, in the order they are listed
    std::vector<std::string> preparable; // spell ids this caster may prepare (empty = not a prepared caster)
    int prepareLimit = 0;                // how many of them it may have prepared at once
    std::vector<std::string> prepared;   // the ones prepared now; they are also in `spells`
    std::vector<Item> inventory;
    int coins = 0; // in the game's smallest coin; they weigh nothing
    std::vector<ActiveCondition> conditions;
    std::set<std::string> proficiencies; // skill ids, ability ids (saves), "weapons", "armor"
    std::map<std::string, std::string> proficiencyRanks; // same targets, plus "dc"; explicit ranks override the legacy list
    std::string dcAbility; // empty: a DC with no ability bonus
    DeathState death;
    std::string notes;

    int abilityScore(std::string_view ability) const { return stats.integer(ability); }
    int abilityModifier(const Ruleset& rules, std::string_view ability) const;
    int maxHp() const { return stats.integer("maxHp"); }
    int armorClass(const Ruleset& rules) const;
    int speedFeet() const { return stats.integer("speed"); }
    // Squares moved in a turn: the speed, less for someone carrying too much (see encumbrance).
    int speedSquares(const Ruleset& rules) const;

    // Ability or skill check: d20 + modifier (+ proficiency if proficient).
    int checkModifier(const Ruleset& rules, std::string_view abilityOrSkill) const;
    int saveModifier(const Ruleset& rules, std::string_view ability) const;
    std::string proficiencyRank(const Ruleset& rules, std::string_view target) const;
    int proficiencyModifier(const Ruleset& rules, std::string_view target) const;
    bool checkProficiencyRanks(const Ruleset& rules, std::string* error = nullptr) const;
    int difficultyClass(const Ruleset& rules, std::string_view ability = {}) const;
    int initiativeModifier(const Ruleset& rules) const;
    RollResult rollCheck(const Ruleset& rules, std::string_view abilityOrSkill, Advantage advantage, Random& random) const;
    RollResult rollSave(const Ruleset& rules, std::string_view ability, Advantage advantage, Random& random) const;

    // Weapon attacks: the equipped main-hand item, or an unarmed strike.
    const Item* weapon() const;
    int attackModifier(const Ruleset& rules) const;
    std::string damageDice(const Ruleset& rules) const;
    // Actions a Strike takes: the weapon's hands where the ruleset says so, otherwise 1.
    int strikeCost(const Ruleset& rules) const;
    // Conditions can force advantage/disadvantage on attacks.
    Advantage attackAdvantage(const Ruleset& rules) const;

    // Damage burns temporary HP first. Returns true if this dropped the character to 0.
    bool takeDamage(int amount);
    bool takeDamage(int amount, const Ruleset& rules, bool critical = false);
    // Reconciles HP, death state and the configured conditions. A dead sheet remains at zero.
    void syncDeath(const Ruleset& rules);
    // Only rolls for a dying sheet when death rules are enabled; no ability or proficiency bonus.
    std::optional<RollResult> rollDeathSave(const Ruleset& rules, Random& random);
    void heal(int amount);
    bool down() const { return hp <= 0; }
    // Applies a rest's or a win's healing. Returns the HP gained; `detail` gets the roll ("2d10+4: ...").
    int recover(const Ruleset& rules, const Recovery& recovery, Random& random, std::string* detail = nullptr);

    // Puts an item on, taking off whatever was in its slot. Held items ("mainHand", "offHand"...)
    // share the character's hands: taking up something that needs more than are free puts the
    // other held items away (a greatsword leaves no hand for a shield).
    static constexpr int handCount = 2;
    static bool held(const Item& item) { return item.slot.ends_with("Hand"); }
    int handsInUse() const;
    int freeHands() const { return handCount - handsInUse(); }
    bool equip(size_t inventoryIndex);
    void unequip(size_t inventoryIndex);

    // Applies a condition for `rounds` (-1 = until something ends it; the default is the
    // definition's own duration). One already there is refreshed, kept if it lasts longer, or
    // raised by `value`, as its definition's stacking says. Conditions it `removes` come off.
    static constexpr int definedDuration = -2;
    void addCondition(const Ruleset& rules, std::string_view id, int rounds = definedDuration, int value = 1);
    void removeCondition(std::string_view id);
    // A modifier of its own that lasts `rounds` (-1 = until removed), tracked like a condition
    // under `id`: it counts down with the rounds and removeCondition(id) takes it off.
    void addModifier(std::string_view id, Modifier modifier, int rounds = -1);
    bool hasCondition(std::string_view id) const;
    int conditionValue(std::string_view id) const; // 0 = doesn't have it
    // One of its conditions carries this flag ("cantAct", "cantMove", or any the game defines).
    bool hasFlag(const Ruleset& rules, std::string_view flag) const;
    // Something happened to it ("damage", "turnStart", "rest"...; see conditionEvents): every
    // condition that ends on that comes off. Returns their ids.
    std::vector<std::string> conditionEvent(const Ruleset& rules, std::string_view event);
    // Call once per round: counts down timed conditions, lets values decay and, given dice, rolls
    // the saves that end conditions. Returns the ids that ended.
    std::vector<std::string> endRound(const Ruleset& rules, Random* random = nullptr);
    // Counts down timed conditions only, for sheets used without a ruleset.
    void endRound();

    float carriedWeight() const;
    // Removes one unequipped unit, keeping the remaining equipment's modifier sources valid.
    bool removeItem(size_t index);
    float carryCapacity(const Ruleset& rules) const;
    // How weighed down: 0 = free, 1 = slowed, 2 = can't move (the ruleset's shares of capacity).
    int encumbrance(const Ruleset& rules) const;
    // Magic items carried, worn or not, counting each of a stack; and whether `more` of them
    // still fit under the ruleset's limit (always, where it has none).
    int magicItems() const;
    bool roomForMagic(const Ruleset& rules, int more = 1) const;

    // Refills resources by name: "*" is all of them, "slots-*" every one starting that way.
    // Returns how many points came back.
    int restoreResources(const std::vector<std::string>& names);

    // Prepared caster: makes `ids` its prepared spells, swapping them in `spells`. At least one,
    // all from `preparable`, no more than `prepareLimit`. `why` gets a short reason if not.
    bool prepare(const std::vector<std::string>& ids, std::string* why = nullptr);

    void addXp(const Ruleset& rules, int amount); // levels up automatically

    // Takes everything a character's choices decide from a freshly built sheet (see
    // Compendium::build) and keeps this sheet's live state: HP lost, temporary HP, conditions and
    // their modifiers, what it carries, death saves and resources spent. HP stays within the new
    // maximum; a resource keeps its current amount up to the new maximum. Modifiers whose source
    // starts "build:" (a feat's) belong to the build and are replaced by the new sheet's.
    void adoptBuild(const Character& built);

    // Save files. Returns false and fills `error` if the JSON is broken.
    std::string toJson() const;
    static std::optional<Character> fromJson(std::string_view json, std::string* error = nullptr);
};

// Rolls a new character: 4d6 keep 3 per ability (or 3d6 for classic), HP = max hit die + CON.
Character makeRandomCharacter(const Ruleset& rules, std::string name, std::string characterClass, Random& random);

}
