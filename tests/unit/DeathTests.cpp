#include "DeathTests.h"
#include "Checks.h"

#include <yorehold/framework/rpg/Combat.h>
#include <yorehold/framework/rpg/Compendium.h>
#include <yorehold/framework/rpg/Effect.h>
#include <nlohmann/json.hpp>

#include <array>

namespace regression
{
namespace
{

yh::Ruleset deathRules()
{
    auto rules = yh::Ruleset::modern(); rules.death.enabled = true;
    rules.death.downedCondition = "fallen"; rules.death.dyingCondition = "dying";
    rules.death.stableCondition = "stable"; rules.death.deadCondition = "dead";
    for (const char* id : {"fallen", "dying", "stable", "dead"})
    {
        yh::ConditionDefinition condition; condition.id = id; condition.name = id; condition.flags = {"cantAct", "cantMove"};
        rules.conditions.push_back(condition);
    }
    return rules;
}

yh::Character sheet()
{
    yh::Character c; c.name = "Ana"; c.hp = 10; c.stats.setBase("maxHp", 10);
    return c;
}

uint64_t seedFor(int face)
{
    for (uint64_t seed = 0; seed < 10000; seed++)
    {
        yh::Random random(seed);
        if (yh::rollD20(0, yh::Advantage::None, random).total == face) return seed;
    }
    throw std::runtime_error("No death-save seed");
}

void states()
{
    const auto rules = deathRules();
    auto c = sheet();
    CHECK(c.takeDamage(10, rules) && c.hp == 0 && c.hasCondition("fallen") && c.hasCondition("dying"));
    const uint64_t success = seedFor(11), failure = seedFor(5);
    for (int i = 0; i < 3; i++)
    {
        yh::Random random(success);
        CHECK(c.rollDeathSave(rules, random)->total == 11 && c.death.successes == i + 1);
    }
    yh::Random random(failure);
    CHECK(c.death.stable && c.hasCondition("stable") && !c.hasCondition("dying") && !c.rollDeathSave(rules, random));
    c.takeDamage(1, rules);
    CHECK(!c.death.stable && c.death.successes == 0 && c.death.failures == 1 && c.hasCondition("dying"));
    c.tempHp = 5; c.takeDamage(5, rules);
    CHECK(c.death.failures == 1 && c.tempHp == 0);
    c.takeDamage(1, rules, true);
    CHECK(c.death.dead && c.hasCondition("dead") && !c.hasCondition("fallen") && !c.hasCondition("dying"));
    c.heal(10);
    CHECK(c.hp == 0 && c.recover(rules, {yh::Recovery::Kind::Full}, random) == 0 && !c.rollDeathSave(rules, random));

    c = sheet(); c.takeDamage(10, rules);
    for (int i = 0; i < 3; i++) { yh::Random fail(failure); c.rollDeathSave(rules, fail); }
    CHECK(c.death.dead && c.death.failures == 3);
    c = sheet(); c.takeDamage(10, rules);
    yh::Random one(seedFor(1)); c.rollDeathSave(rules, one);
    CHECK(c.death.failures == 2 && !c.death.dead);
    yh::Random twenty(seedFor(20)); c.rollDeathSave(rules, twenty);
    CHECK(c.hp == 1 && c.death.failures == 0 && c.death.successes == 0 && c.conditions.empty());
    c.takeDamage(1, rules); c.heal(2); c.syncDeath(rules);
    CHECK(c.hp == 2 && c.conditions.empty());
    c = sheet(); c.death.saves = false; c.takeDamage(10, rules);
    CHECK(c.death.dead && !c.rollDeathSave(rules, random));
    auto legacy = sheet(); legacy.takeDamage(10, yh::Ruleset::modern());
    CHECK(!legacy.death.dead && !legacy.rollDeathSave(yh::Ruleset::modern(), random));
    legacy.heal(1); CHECK(legacy.hp == 1);
}

void turns()
{
    for (const bool shared : {false, true})
    {
        auto rules = deathRules(); rules.sharedTurns = shared; rules.death.saveDc = -1000; rules.death.naturalTwentyHp = 0;
        std::array<yh::Character, 3> people{sheet(), sheet(), sheet()};
        for (size_t i = 0; i < people.size(); i++) people[i].stats.setBase("dex", 2000.0f - static_cast<float>(i) * 200);
        people[0].takeDamage(10, rules);
        yh::Encounter fight(rules, 7);
        fight.add(people[0], 0); fight.add(people[1], 0); fight.add(people[2], 1); fight.start();
        CHECK(fight.current().character == &people[1] && people[0].death.successes + people[0].death.failures > 0);
        const int first = people[0].death.successes + people[0].death.failures;
        fight.nextTurn();
        CHECK(fight.current().character == &people[2] && people[0].death.successes + people[0].death.failures == first);
        fight.nextTurn();
        CHECK(fight.round() == 2 && people[0].death.successes + people[0].death.failures > first);
    }
}

void filesAndEffects()
{
    const auto rules = deathRules();
    const auto restoredRules = yh::Ruleset::fromJson(rules.toJson());
    CHECK(rules.checkDeathRules() && restoredRules && restoredRules->toJson() == rules.toJson());
    auto bad = nlohmann::json::parse(rules.toJson()); bad["death"]["successes"] = 0;
    CHECK(!yh::Ruleset::fromJson(bad.dump()));
    bad = nlohmann::json::parse(rules.toJson()); bad["death"]["stableCondition"] = "dead";
    CHECK(!yh::Ruleset::fromJson(bad.dump()));
    auto missing = rules; missing.death.deadCondition = "unknown";
    std::string error; CHECK(!missing.checkDeathRules(&error) && error.find("unknown") != std::string::npos);
    auto c = sheet(); c.takeDamage(10, rules); c.death.successes = 1; c.death.failures = 2;
    CHECK(yh::Character::fromJson(c.toJson())->toJson() == c.toJson());
    bad = nlohmann::json::parse(c.toJson()); bad["death"]["failures"] = -1;
    CHECK(!yh::Character::fromJson(bad.dump()));
    bad = nlohmann::json::parse(c.toJson()); bad.erase("death");
    CHECK(yh::Character::fromJson(bad.dump())->death.saves && yh::Character::fromJson(bad.dump())->death.failures == 0);

    const auto definition = yh::Compendium::classFromJson(R"({"id":"soldier","resources":{"supply":{"max":2}}})");
    const auto creature = yh::Compendium::creatureFromJson(R"({"id":"guard","deathSaves":true,"resources":{"supply":{"max":2,"current":1}}})");
    CHECK(definition && creature && definition->resources.at("supply").current == 2 && creature->deathSaves);
    if (!definition || !creature) return;
    CHECK(!yh::Compendium::classFromJson(R"({"id":"bad","resources":{"supply":{"max":1,"current":2}}})"));
    yh::Compendium compendium; compendium.classes["soldier"] = *definition; compendium.creatures["guard"] = *creature;
    yh::Random random(7);
    CHECK(compendium.makeCharacter(rules, "soldier", "", random)->resources.at("supply").current == 2);
    CHECK(compendium.makeCreature(rules, "guard", "", random)->death.saves);

    struct Host : yh::EffectHost
    {
        std::array<yh::Character, 2> people{regression::sheet(), regression::sheet()};
        bool critical = false;
        yh::Character* sheet(yh::EffectActor who) override { return &people[static_cast<size_t>(who)]; }
        std::vector<yh::EffectActor> group(std::string_view, const yh::EffectContext&) override { return {}; }
        int damage(yh::EffectActor who, int amount, std::string_view type, const yh::EffectContext& context) override
        {
            critical = context.criticalDamage;
            return yh::EffectHost::damage(who, amount, type, context);
        }
    } host;
    host.people[0].takeDamage(10, rules); host.people[0].death.failures = 1;
    yh::Random crit(seedFor(20));
    yh::EffectContext context; context.self = 1; context.targets = {0}; context.rules = &rules; context.random = &crit;
    const auto effect = yh::Effect::fromJson(R"([{"do":"roll","kind":"attack","steps":[{"do":"damage","dice":1}]}])");
    CHECK(effect && !effect->run(host, context).events.empty() && host.critical && host.people[0].death.dead);
}

}

void deathSaves()
{
    states();
    turns();
    filesAndEffects();
}

}
