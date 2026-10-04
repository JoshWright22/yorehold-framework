#include "yorehold/framework/rpg/Combat.h"

#include <algorithm>

namespace yh
{

void Encounter::add(Character& character, int team)
{
    if (started_) return;
    for (const auto& c : order_) if (c.character == &character) return;
    Combatant c;
    c.character = &character;
    c.team = team;
    order_.push_back(c);
}

void Encounter::join(Character& character, int team)
{
    if (!started_)
    {
        add(character, team);
        return;
    }
    for (const auto& c : order_) if (c.character == &character) return;
    Combatant c;
    c.character = &character;
    c.team = team;
    c.initiativeRoll = rollD20(character.initiativeModifier(rules_), Advantage::None, random_);
    c.initiative = c.initiativeRoll.total;
    addLog(character.name + " joins the fight, initiative " + c.initiativeRoll.describe());
    // After everyone who rolled at least as high; whoever's turn it is keeps it.
    size_t at = 0;
    while (at < order_.size() && order_[at].initiative >= c.initiative)
        at++;
    order_.insert(order_.begin() + static_cast<std::ptrdiff_t>(at), c);
    if (at <= current_)
        current_++;
}

void Encounter::surprise(int team)
{
    if (started_) return;
    for (Combatant& c : order_)
        if (c.team == team)
            c.surprised = true;
}

void Encounter::withdraw(size_t index, const std::string& why)
{
    if (index >= order_.size() || order_[index].out)
        return;
    order_[index].out = true;
    if (!why.empty())
        addLog(order_[index].character->name + " " + why);
}

void Encounter::start()
{
    if (started_ || order_.empty()) return;
    for (Combatant& c : order_)
    {
        c.initiativeRoll = rollD20(c.character->initiativeModifier(rules_), Advantage::None, random_);
        c.initiative = c.initiativeRoll.total;
        addLog(c.character->name + " initiative " + c.initiativeRoll.describe());
    }
    std::stable_sort(order_.begin(), order_.end(), [&](const Combatant& a, const Combatant& b) {
        if (a.initiative != b.initiative)
            return a.initiative > b.initiative;
        return a.character->initiativeModifier(rules_) > b.character->initiativeModifier(rules_);
    });
    started_ = true;
    addLog("Round 1");
    for (const Combatant& c : order_)
        if (c.surprised && c.standing())
            addLog(c.character->name + " is caught by surprise");
    // Whoever goes first, unless they're down or surprised (then the next one that can).
    current_ = order_.size() - 1;
    round_ = 0;
    nextTurn();
    if (round_ == 0)
    {
        // Nothing to fight (one side only): the fight is over before it begins.
        round_ = 1;
        current_ = 0;
        if (order_[current_].standing())
            beginTurn();
    }
}

void Encounter::beginTurn()
{
    Combatant& c = order_[current_];
    conditionsEnded(*c.character, c.character->conditionEvent(rules_, "turnStart"));
    c.budget = {rules_.actionsPerTurn, rules_.bonusActions, true, c.character->speedSquares(rules_)};
    // Conditions can take the turn's actions or movement away.
    if (c.character->hasFlag(rules_, "cantAct"))
    {
        c.budget.actions = 0;
        c.budget.bonusAction = false;
        c.budget.reaction = false;
    }
    if (c.character->hasFlag(rules_, "cantMove"))
        c.budget.movementLeft = 0;
    addLog(c.character->name + "'s turn");
}

void Encounter::conditionsEnded(const Character& character, const std::vector<std::string>& ids)
{
    for (const std::string& id : ids)
    {
        const ConditionDefinition* def = rules_.condition(id);
        addLog(character.name + " is no longer " + (def ? def->name : id));
    }
}

void Encounter::nextTurn()
{
    if (!started_ || finished())
        return;
    if (round_ > 0 && order_[current_].standing())
        conditionsEnded(*order_[current_].character, order_[current_].character->conditionEvent(rules_, "turnEnd"));
    // Twice round: a surprised combatant passes once and can then take the next turn that comes.
    for (size_t tries = 0; tries < order_.size() * 2 + 1; tries++)
    {
        current_++;
        if (current_ >= order_.size())
        {
            current_ = 0;
            if (round_ > 0)
            {
                for (Combatant& c : order_)
                    conditionsEnded(*c.character, c.character->endRound(rules_, &random_));
                round_++;
                addLog("Round " + std::to_string(round_));
            }
            else
                round_ = 1;
        }
        Combatant& c = order_[current_];
        if (!c.standing())
            continue;
        if (c.surprised)
        {
            c.surprised = false;
            addLog(c.character->name + " is surprised and loses the turn");
            continue;
        }
        break;
    }
    beginTurn();
}

AttackResult Encounter::attack(size_t targetIndex)
{
    AttackResult result;
    if (!canStrike() || targetIndex >= order_.size() || !order_[targetIndex].standing())
        return result;
    Combatant& attacker = order_[current_];
    Character& target = *order_[targetIndex].character;
    Character& self = *attacker.character;
    attacker.budget.actions -= strikeCost();

    result.attackRoll = rollD20(self.attackModifier(rules_), self.attackAdvantage(rules_), random_);
    const std::vector<std::string> afterAttack = self.conditionEvent(rules_, "attack"); // they still count for this roll
    result.critical = result.attackRoll.natural20();
    const int ac = target.armorClass(rules_);
    result.hit = !result.attackRoll.natural1() && (result.critical || result.attackRoll.total >= ac);

    std::string line = self.name + " attacks " + target.name + " (AC " + std::to_string(ac) + "): " + result.attackRoll.describe();
    if (!result.hit)
    {
        addLog(line + " - miss");
        conditionsEnded(self, afterAttack);
        return result;
    }

    std::optional<DiceExpression> damage = DiceExpression::parse(self.damageDice(rules_));
    if (damage && result.critical)
    {
        for (DiceTerm& term : damage->terms)
        {
            if (term.sides > 0)
                term.count *= 2;
        }
    }
    result.damageRoll = damage ? roll(*damage, random_) : RollResult{};
    const int dealt = std::max(1, result.damageRoll.total);
    result.targetDropped = target.takeDamage(dealt);
    line += result.critical ? " - CRITICAL HIT, " : " - hit, ";
    line += result.damageRoll.describe() + " damage";
    if (result.targetDropped)
        line += ". " + target.name + " goes down!";
    addLog(line);
    conditionsEnded(self, afterAttack);
    conditionsEnded(target, target.conditionEvent(rules_, "damage"));
    return result;
}

bool Encounter::dash()
{
    if (!canAct())
        return false;
    Combatant& c = order_[current_];
    c.budget.actions--;
    c.budget.movementLeft += c.character->speedSquares(rules_);
    addLog(c.character->name + " dashes");
    return true;
}

int Encounter::strikeCost() const
{
    if (order_.empty()) return 1;
    return order_[current_].character->strikeCost(rules_);
}

bool Encounter::spendActions(int count)
{
    if (count < 0 || !canAct(count)) return false;
    order_[current_].budget.actions -= count;
    return true;
}

bool Encounter::useReaction(size_t index)
{
    if (!started_ || finished() || index >= order_.size() || !order_[index].standing() || !order_[index].budget.reaction
        || order_[index].character->hasFlag(rules_, "cantAct"))
        return false;
    order_[index].budget.reaction = false;
    return true;
}

bool Encounter::spendMovement(int squares)
{
    if (!started_ || order_.empty() || finished() || squares < 0 || !order_[current_].standing()) return false;
    TurnBudget& budget = order_[current_].budget;
    if (squares > budget.movementLeft)
        return false;
    budget.movementLeft -= squares;
    return true;
}

bool Encounter::finished() const
{
    int team = 0;
    bool haveTeam = false;
    for (const auto& c : order_)
    {
        if (!c.standing()) continue;
        if (haveTeam && c.team != team) return false;
        team = c.team;
        haveTeam = true;
    }
    return true;
}

int Encounter::winningTeam() const
{
    int team = -1;
    for (const Combatant& c : order_)
    {
        if (!c.standing())
            continue;
        if (team >= 0 && c.team != team)
            return -1;
        team = c.team;
    }
    return team;
}

void Encounter::addLog(std::string line)
{
    log_.push_back(std::move(line));
}

}
