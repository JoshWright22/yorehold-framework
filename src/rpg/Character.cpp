#include "yorehold/framework/rpg/Character.h"

#include "yorehold/framework/rpg/Dice.h"

#include <algorithm>
#include <cmath>

namespace yh
{

namespace
{

std::string conditionSource(std::string_view id)
{
    return "condition:" + std::string(id);
}

std::string itemSource(const Item& item, size_t index)
{
    return "item:" + item.id + "#" + std::to_string(index);
}

}

int Character::abilityModifier(const Ruleset& rules, std::string_view ability) const
{
    return rules.abilityModifier(abilityScore(ability));
}

int Character::armorClass(const Ruleset& rules) const
{
    // "ac" holds armour: its base is the ruleset's unarmoured AC, armour overrides it, shields add.
    const int dex = rules.armorClassAbility.empty() ? 0 : abilityModifier(rules, rules.armorClassAbility);
    return stats.integer("ac") + dex;
}

int Character::checkModifier(const Ruleset& rules, std::string_view abilityOrSkill) const
{
    if (const SkillDefinition* skill = rules.skill(abilityOrSkill))
    {
        const int proficient = proficiencies.contains(skill->id) ? rules.proficiencyBonus(level) : 0;
        return abilityModifier(rules, skill->ability) + proficient;
    }
    return abilityModifier(rules, abilityOrSkill);
}

int Character::saveModifier(const Ruleset& rules, std::string_view ability) const
{
    const int proficient = proficiencies.contains(std::string(ability)) ? rules.proficiencyBonus(level) : 0;
    return abilityModifier(rules, ability) + proficient;
}

int Character::initiativeModifier(const Ruleset& rules) const
{
    return rules.initiativeAbility.empty() ? 0 : abilityModifier(rules, rules.initiativeAbility);
}

RollResult Character::rollCheck(const Ruleset& rules, std::string_view abilityOrSkill, Advantage advantage, Random& random) const
{
    return rollD20(checkModifier(rules, abilityOrSkill), advantage, random);
}

RollResult Character::rollSave(const Ruleset& rules, std::string_view ability, Advantage advantage, Random& random) const
{
    return rollD20(saveModifier(rules, ability), advantage, random);
}

const Item* Character::weapon() const
{
    for (const Item& item : inventory)
    {
        if (item.equipped && item.slot == "mainHand")
            return &item;
    }
    return nullptr;
}

int Character::attackModifier(const Ruleset& rules) const
{
    const Item* held = weapon();
    const int ability = abilityModifier(rules, held ? held->attackAbility : "str");
    const int proficient = proficiencies.contains("weapons") ? rules.proficiencyBonus(level) : 0;
    return ability + proficient + stats.integer("attack");
}

std::string Character::damageDice(const Ruleset& rules) const
{
    const Item* held = weapon();
    const int bonus = abilityModifier(rules, held ? held->attackAbility : "str") + stats.integer("damage");
    std::string dice = held && !held->damage.empty() ? held->damage : "1";
    if (bonus != 0)
        dice += (bonus > 0 ? "+" : "") + std::to_string(bonus);
    return dice;
}

Advantage Character::attackAdvantage(const Ruleset& rules) const
{
    bool advantage = false;
    bool disadvantage = false;
    for (const ActiveCondition& active : conditions)
    {
        if (const ConditionDefinition* def = rules.condition(active.id))
        {
            advantage |= def->advantageOnAttacks;
            disadvantage |= def->disadvantageOnAttacks;
        }
    }
    // Both cancel out.
    if (advantage == disadvantage)
        return Advantage::None;
    return advantage ? Advantage::Advantage : Advantage::Disadvantage;
}

bool Character::takeDamage(int amount)
{
    const bool wasUp = hp > 0;
    const int absorbed = std::min(tempHp, amount);
    tempHp -= absorbed;
    hp = std::max(0, hp - (amount - absorbed));
    return wasUp && hp == 0;
}

void Character::heal(int amount)
{
    hp = std::min(maxHp(), hp + amount);
}

int Character::recover(const Ruleset& rules, const Recovery& recovery, Random& random, std::string* detail)
{
    if (detail) detail->clear();
    if (down() && !recovery.reviveDowned)
        return 0;
    const int before = std::max(0, hp);
    int amount = 0;
    switch (recovery.kind)
    {
    case Recovery::Kind::None: return 0;
    case Recovery::Kind::Full: amount = maxHp(); break;
    case Recovery::Kind::Fraction: amount = static_cast<int>(std::ceil(maxHp() * recovery.fraction)); break;
    case Recovery::Kind::Flat: amount = recovery.amount; break;
    case Recovery::Kind::HitDice:
    {
        const int dice = recovery.amount > 0 ? recovery.amount : std::max(1, level);
        const int bonus = rules.hitDieAbility.empty() ? 0 : abilityModifier(rules, rules.hitDieAbility) * dice;
        const std::string expression = std::to_string(dice) + "d" + std::to_string(rules.hitDie(characterClass))
            + (bonus < 0 ? "" : "+") + std::to_string(bonus);
        const RollResult roll = yh::roll(expression, random);
        if (detail) *detail = roll.describe();
        amount = std::max(1, roll.total); // resting always helps a little
        break;
    }
    }
    hp = std::min(maxHp(), before + amount);
    return hp - before;
}

bool Character::equip(size_t index)
{
    if (index >= inventory.size() || inventory[index].slot.empty() || inventory[index].equipped)
        return false;
    // One item per slot: take off whatever's there.
    for (size_t i = 0; i < inventory.size(); i++)
    {
        if (inventory[i].equipped && inventory[i].slot == inventory[index].slot)
            unequip(i);
    }
    Item& item = inventory[index];
    item.equipped = true;
    for (Modifier m : item.modifiers)
    {
        m.source = itemSource(item, index);
        stats.addModifier(std::move(m));
    }
    return true;
}

void Character::unequip(size_t index)
{
    if (index >= inventory.size() || !inventory[index].equipped)
        return;
    inventory[index].equipped = false;
    stats.removeSource(itemSource(inventory[index], index));
}

void Character::addCondition(const Ruleset& rules, std::string_view id, int rounds)
{
    removeCondition(id); // re-applying refreshes the duration
    conditions.push_back({std::string(id), rounds});
    if (const ConditionDefinition* def = rules.condition(id))
    {
        for (Modifier m : def->modifiers)
        {
            m.source = conditionSource(id);
            stats.addModifier(std::move(m));
        }
    }
}

void Character::removeCondition(std::string_view id)
{
    std::erase_if(conditions, [&](const ActiveCondition& c) { return c.id == id; });
    stats.removeSource(conditionSource(id));
}

bool Character::hasCondition(std::string_view id) const
{
    return std::any_of(conditions.begin(), conditions.end(), [&](const ActiveCondition& c) { return c.id == id; });
}

void Character::endRound()
{
    std::vector<std::string> expired;
    for (ActiveCondition& c : conditions)
    {
        if (c.roundsLeft > 0 && --c.roundsLeft == 0)
            expired.push_back(c.id);
    }
    for (const std::string& id : expired)
        removeCondition(id);
}

float Character::carriedWeight() const
{
    float total = 0;
    for (const Item& item : inventory)
        total += item.weight * static_cast<float>(item.quantity);
    return total;
}

float Character::carryCapacity(const Ruleset& rules) const
{
    const std::string& strength = rules.abilities.empty() ? std::string("str") : rules.abilities.front().id;
    return static_cast<float>(abilityScore(strength) * rules.carryPerStrength);
}

void Character::addXp(const Ruleset& rules, int amount)
{
    xp += amount;
    level = std::max(level, rules.levelForXp(xp));
}

Character makeRandomCharacter(const Ruleset& rules, std::string name, std::string characterClass, Random& random)
{
    Character c;
    c.name = std::move(name);
    c.characterClass = std::move(characterClass);
    const char* abilityDice = rules.modifierTable == ModifierTable::Classic ? "3d6" : "4d6kh3";
    for (const AbilityDefinition& ability : rules.abilities)
        c.stats.setBase(ability.id, static_cast<float>(roll(abilityDice, random).total));
    c.stats.setBase("ac", static_cast<float>(rules.baseArmorClass));
    c.stats.setBase("speed", 30);

    const std::optional<DiceExpression> hitDie = DiceExpression::parse(c.hitDie);
    const int hp = std::max(1, (hitDie ? hitDie->maximum() : 8) + c.abilityModifier(rules, "con"));
    c.stats.setBase("maxHp", static_cast<float>(hp));
    c.hp = hp;
    return c;
}

}
