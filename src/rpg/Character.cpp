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

// A condition's modifiers at `value`, tagged so removing the condition takes them off again.
void addConditionModifiers(StatBlock& stats, const ConditionDefinition& def, int value)
{
    for (Modifier m : def.modifiers)
    {
        m.source = conditionSource(def.id);
        if (def.perValue && m.op == Modifier::Op::Add)
            m.value *= static_cast<float>(value);
        stats.addModifier(std::move(m));
    }
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
    return stats.integer("ac") + dex + (rules.proficiencyRanks.empty() ? 0 : proficiencyModifier(rules, "armor"));
}

std::string Character::proficiencyRank(const Ruleset& rules, std::string_view target) const
{
    const auto explicitRank = proficiencyRanks.find(std::string(target));
    if (explicitRank != proficiencyRanks.end()) return explicitRank->second;
    return proficiencies.contains(std::string(target)) ? rules.proficientRank : rules.untrainedRank;
}

int Character::proficiencyModifier(const Ruleset& rules, std::string_view target) const
{
    if (rules.proficiencyRanks.empty())
        return proficiencies.contains(std::string(target)) ? rules.proficiencyBonus(level) : 0;
    return rules.proficiencyBonus(level, proficiencyRank(rules, target));
}

bool Character::checkProficiencyRanks(const Ruleset& rules, std::string* error) const
{
    if (error) error->clear();
    // Explicit choices are retained but inactive under older table-based rulesets.
    if (rules.proficiencyRanks.empty()) return true;
    auto fail = [&](std::string reason) { if (error) *error = std::move(reason); return false; };
    if (!dcAbility.empty() && !rules.ability(dcAbility)) return fail("Unknown DC ability: " + dcAbility);
    for (const auto& [target, rank] : proficiencyRanks)
    {
        if (target != "weapons" && target != "armor" && target != "dc" && !rules.skill(target) && !rules.ability(target))
            return fail("Unknown proficiency target: " + target);
        if (!rules.proficiencyRank(rank)) return fail("Unknown proficiency rank: " + rank);
    }
    return true;
}

int Character::difficultyClass(const Ruleset& rules, std::string_view ability) const
{
    const std::string_view used = ability.empty() ? std::string_view(dcAbility) : ability;
    const int modifier = rules.ability(used) ? abilityModifier(rules, used) : 0;
    return rules.baseDc + modifier + proficiencyModifier(rules, "dc") + stats.integer("dc");
}

int Character::checkModifier(const Ruleset& rules, std::string_view abilityOrSkill) const
{
    if (const SkillDefinition* skill = rules.skill(abilityOrSkill))
    {
        const int proficient = proficiencyModifier(rules, skill->id);
        return abilityModifier(rules, skill->ability) + proficient;
    }
    return abilityModifier(rules, abilityOrSkill);
}

int Character::saveModifier(const Ruleset& rules, std::string_view ability) const
{
    const int proficient = proficiencyModifier(rules, ability);
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

int Character::strikeCost(const Ruleset& rules) const
{
    const Item* held = weapon();
    return rules.strikeCostsHands && held ? std::clamp(held->hands, 1, rules.actionsPerTurn) : 1;
}

int Character::attackModifier(const Ruleset& rules) const
{
    const Item* held = weapon();
    const int ability = abilityModifier(rules, held ? held->attackAbility : "str");
    const int proficient = proficiencyModifier(rules, "weapons");
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
    amount = std::max(0, amount);
    const bool wasUp = hp > 0;
    const int absorbed = std::min(tempHp, amount);
    tempHp -= absorbed;
    hp = std::max(0, hp - (amount - absorbed));
    return wasUp && hp == 0;
}

void Character::heal(int amount)
{
    if (death.dead) return;
    hp = std::min(maxHp(), hp + std::max(0, amount));
    if (hp > 0) death = {death.saves};
}

int Character::recover(const Ruleset& rules, const Recovery& recovery, Random& random, std::string* detail)
{
    if (detail) detail->clear();
    if (death.dead || (down() && !recovery.reviveDowned))
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
    heal(amount);
    syncDeath(rules);
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

void Character::addCondition(const Ruleset& rules, std::string_view id, int rounds, int value)
{
    const ConditionDefinition* def = rules.condition(id);
    if (rounds == definedDuration)
        rounds = def ? def->duration : -1;
    value = std::max(1, value);
    if (def)
    {
        const auto old = std::find_if(conditions.begin(), conditions.end(), [&](const ActiveCondition& c) { return c.id == id; });
        if (old != conditions.end() && def->stacking == ConditionDefinition::Stacking::Longest
            && (old->roundsLeft < 0 || (rounds >= 0 && old->roundsLeft > rounds)))
            rounds = old->roundsLeft;
        if (def->stacking == ConditionDefinition::Stacking::Value)
            value = std::min(def->maxValue, value + (old != conditions.end() ? old->value : 0));
        else
            value = 1;
    }
    removeCondition(id); // whatever was there is replaced by what was just worked out
    conditions.push_back({std::string(id), rounds, value});
    if (!def)
        return;
    addConditionModifiers(stats, *def, value);
    for (const std::string& other : def->removes)
        if (other != id)
            removeCondition(other);
}

void Character::removeCondition(std::string_view id)
{
    std::erase_if(conditions, [&](const ActiveCondition& c) { return c.id == id; });
    stats.removeSource(conditionSource(id));
}

void Character::addModifier(std::string_view id, Modifier modifier, int rounds)
{
    const auto old = std::find_if(conditions.begin(), conditions.end(), [&](const ActiveCondition& c) { return c.id == id; });
    if (old != conditions.end())
        old->roundsLeft = rounds;
    else
        conditions.push_back({std::string(id), rounds, 1});
    modifier.source = conditionSource(id);
    stats.addModifier(std::move(modifier));
}

bool Character::hasCondition(std::string_view id) const
{
    return std::any_of(conditions.begin(), conditions.end(), [&](const ActiveCondition& c) { return c.id == id; });
}

int Character::conditionValue(std::string_view id) const
{
    const auto found = std::find_if(conditions.begin(), conditions.end(), [&](const ActiveCondition& c) { return c.id == id; });
    return found == conditions.end() ? 0 : found->value;
}

bool Character::hasFlag(const Ruleset& rules, std::string_view flag) const
{
    return std::any_of(conditions.begin(), conditions.end(), [&](const ActiveCondition& c) {
        const ConditionDefinition* def = rules.condition(c.id);
        return def && def->hasFlag(flag);
    });
}

std::vector<std::string> Character::conditionEvent(const Ruleset& rules, std::string_view event)
{
    std::vector<std::string> ended;
    for (const ActiveCondition& c : conditions)
    {
        const ConditionDefinition* def = rules.condition(c.id);
        if (def && def->endsOn(event))
            ended.push_back(c.id);
    }
    for (const std::string& id : ended)
        removeCondition(id);
    return ended;
}

std::vector<std::string> Character::endRound(const Ruleset& rules, Random* random)
{
    std::vector<std::string> ended;
    std::vector<std::pair<std::string, int>> weaker; // conditions whose value dropped, and what is left
    for (ActiveCondition& c : conditions)
    {
        const ConditionDefinition* def = rules.condition(c.id);
        if (c.roundsLeft > 0 && --c.roundsLeft == 0)
            ended.push_back(c.id);
        else if (def && def->decay > 0 && c.value <= def->decay)
            ended.push_back(c.id);
        else if (def && !def->saveAbility.empty() && random && rollSave(rules, def->saveAbility, Advantage::None, *random).total >= def->saveDc)
            ended.push_back(c.id);
        else if (def && def->decay > 0)
            weaker.emplace_back(c.id, c.value - def->decay);
    }
    for (const std::string& id : ended)
        removeCondition(id);
    for (const auto& [id, value] : weaker)
    {
        // Put back at its lower value with the time it had left, so its modifiers follow.
        const auto found = std::find_if(conditions.begin(), conditions.end(), [&](const ActiveCondition& c) { return c.id == id; });
        const int rounds = found->roundsLeft;
        removeCondition(id);
        conditions.push_back({id, rounds, value});
        addConditionModifiers(stats, *rules.condition(id), value);
    }
    return ended;
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

void Character::adoptBuild(const Character& built)
{
    name = built.name;
    ancestry = built.ancestry;
    characterClass = built.characterClass;
    level = built.level;
    xp = std::max(xp, built.xp);
    hitDie = built.hitDie;
    notes = built.notes;
    for (const auto& [stat, value] : built.stats.bases())
        stats.setBase(stat, value);
    proficiencies = built.proficiencies;
    proficiencyRanks = built.proficiencyRanks;
    dcAbility = built.dcAbility;
    for (const auto& [id, resource] : built.resources)
    {
        const auto live = resources.find(id);
        resources[id] = {live == resources.end() ? resource.current : std::clamp(live->second.current, 0, resource.max), resource.max};
    }
    hp = std::min(hp, maxHp());
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
