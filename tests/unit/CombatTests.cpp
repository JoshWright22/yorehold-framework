#include "CombatTests.h"
#include "Checks.h"

#include <yorehold/framework/rpg/Combat.h>

#include <array>

namespace regression
{

void sharedTurns()
{
    yh::Ruleset rules = yh::Ruleset::modern();
    CHECK(!rules.sharedTurns);
    rules.sharedTurns = true;
    CHECK(yh::Ruleset::fromJson(rules.toJson())->sharedTurns);
    yh::ConditionDefinition start, end;
    start.id = "start"; start.ends = {"turnStart"};
    end.id = "end"; end.ends = {"turnEnd"};
    rules.conditions.push_back(start); rules.conditions.push_back(end);
    yh::Random random(1);
    std::array<yh::Character, 5> people;
    for (size_t i = 0; i < people.size(); i++)
    {
        people[i] = yh::makeRandomCharacter(rules, std::to_string(i), "Fighter", random);
        people[i].stats.setBase("dex", 2000.0f - static_cast<float>(i) * 200.0f);
        people[i].hp = 200;
    }
    yh::Encounter fight(rules, 7);
    for (size_t i = 0; i < people.size(); i++) fight.add(people[i], i == 2 || i == 4 ? 1 : 0);
    fight.start();
    CHECK(fight.round() == 1 && fight.currentIndex() == 0 && fight.blockFirst() == 0 && fight.blockEnd() == 2);
    CHECK(fight.canSelectTurn(1) && !fight.canSelectTurn(2) && !fight.canSelectTurn(3) && !fight.selectTurn(999));
    const uint64_t serial = fight.blockSerial();
    people[0].addCondition(rules, "start"); people[0].addCondition(rules, "end");
    CHECK(fight.spendActions(1) && fight.spendMovement(1) && fight.useReaction(0));
    const yh::TurnBudget left = fight.current().budget;
    CHECK(fight.selectTurn(1) && fight.current().budget.actions == 2 && fight.blockSerial() == serial);
    CHECK(fight.selectTurn(0) && fight.current().budget.actions == left.actions
        && fight.current().budget.movementLeft == left.movementLeft && !fight.current().budget.reaction);
    CHECK(people[0].hasCondition("start") && people[0].hasCondition("end"));
    fight.nextTurn();
    CHECK(fight.currentIndex() == 1 && !fight.canSelectTurn(0) && !people[0].hasCondition("end"));
    fight.nextTurn();
    CHECK(fight.currentIndex() == 2 && fight.round() == 1 && !fight.canSelectTurn(3));
    fight.nextTurn();
    CHECK(fight.currentIndex() == 3 && fight.canSelectTurn(3) && !fight.canSelectTurn(0));
    fight.nextTurn(); fight.nextTurn();
    CHECK(fight.round() == 2 && fight.currentIndex() == 0 && !people[0].hasCondition("start")
        && fight.current().budget.reaction && fight.current().budget.actions == 2);

    yh::Encounter together(rules, 7);
    together.add(people[0], 0); together.add(people[1], 0); together.add(people[2], 1); together.add(people[3], 1);
    together.surprise(0); together.start();
    CHECK(together.round() == 1 && together.currentIndex() == 2 && together.blockEnd() == 4 && together.canSelectTurn(3));
    CHECK(together.selectTurn(3)); together.nextTurn();
    CHECK(together.currentIndex() == 2 && !together.canSelectTurn(3));
    together.nextTurn();
    CHECK(together.round() == 2 && together.currentIndex() == 0 && together.canSelectTurn(1));
    together.withdraw(1); together.nextTurn();
    CHECK(together.currentIndex() == 2 && !together.canSelectTurn(1));

    yh::Character reinforcement = people[4];
    reinforcement.stats.setBase("dex", 1900);
    yh::Encounter joining(rules, 7);
    joining.add(people[0], 0); joining.add(people[1], 0); joining.add(people[2], 1); joining.start();
    joining.join(reinforcement, 1);
    CHECK(joining.current().character == &people[0] && joining.blockEnd() == 3 && !joining.canSelectTurn(1)
        && joining.canSelectTurn(2));
    joining.nextTurn();
    CHECK(joining.current().character == &people[1]);
    joining.nextTurn();
    CHECK(joining.current().character == &people[2]);
    joining.nextTurn(); joining.nextTurn();
    CHECK(joining.round() == 2 && joining.current().character == &reinforcement);

    people[1].hp = 0;
    yh::Encounter down(rules, 7);
    down.add(people[0], 0); down.add(people[1], 0); down.add(people[2], 1); down.start();
    CHECK(!down.canSelectTurn(1));
    people[1].hp = 200;
    CHECK(!down.canSelectTurn(1));
    down.nextTurn();
    CHECK(down.current().character == &people[2]);

    rules.sharedTurns = false;
    yh::Encounter separate(rules, 7);
    separate.add(people[0], 0); separate.add(people[1], 0); separate.add(people[2], 1); separate.start();
    CHECK(!separate.selectTurn(1) && separate.blockEnd() == separate.currentIndex() + 1);
}

}
