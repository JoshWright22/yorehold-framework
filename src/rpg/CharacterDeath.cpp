#include "yorehold/framework/rpg/Character.h"

#include <algorithm>

namespace yh
{

void Character::syncDeath(const Ruleset& rules)
{
    if (!rules.death.enabled) return;
    if (death.dead) { hp = 0; death.stable = false; }
    if (hp > 0) death = {death.saves};
    else if (!death.saves) { death.dead = true; death.stable = false; }
    const auto& rule = rules.death;
    auto condition = [&](const std::string& id, bool active) {
        if (id.empty()) return;
        if (active && !hasCondition(id)) addCondition(rules, id);
        else if (!active && hasCondition(id)) removeCondition(id);
    };
    condition(rule.downedCondition, down() && !death.dead);
    condition(rule.dyingCondition, down() && !death.dead && !death.stable);
    condition(rule.stableCondition, down() && !death.dead && death.stable);
    condition(rule.deadCondition, death.dead);
}

bool Character::takeDamage(int amount, const Ruleset& rules, bool critical)
{
    syncDeath(rules);
    const bool wasDown = down();
    const int harm = std::max(0, amount - tempHp);
    const bool dropped = takeDamage(amount);
    if (rules.death.enabled && wasDown && harm > 0 && death.saves && !death.dead)
    {
        if (death.stable) { death.successes = 0; death.failures = 0; }
        death.stable = false;
        death.failures = std::min(rules.death.failures, death.failures
            + (critical ? rules.death.criticalDamageFailures : rules.death.damageFailures));
        death.dead = death.failures >= rules.death.failures;
    }
    syncDeath(rules);
    return dropped;
}

std::optional<RollResult> Character::rollDeathSave(const Ruleset& rules, Random& random)
{
    syncDeath(rules);
    if (!rules.death.enabled || !down() || !death.saves || death.stable || death.dead) return std::nullopt;
    const RollResult result = rollD20(0, Advantage::None, random);
    if (result.natural20() && rules.death.naturalTwentyHp > 0)
        heal(rules.death.naturalTwentyHp);
    else if (result.natural1() || result.total < rules.death.saveDc)
    {
        death.failures = std::min(rules.death.failures, death.failures + (result.natural1() ? rules.death.naturalOneFailures : 1));
        death.dead = death.failures >= rules.death.failures;
    }
    else
    {
        death.successes = std::min(rules.death.successes, death.successes + 1);
        death.stable = death.successes >= rules.death.successes;
    }
    syncDeath(rules);
    return result;
}

}
