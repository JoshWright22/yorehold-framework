#include "Checks.h"
#include "ConditionTests.h"

#include <yorehold/framework/assets/FileSystem.h>
#include <yorehold/framework/rpg/Combat.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace regression
{
namespace
{

namespace fs = std::filesystem;

// A small ruleset with one condition of each kind the machinery knows.
const char* authored = R"({"id":"t","name":"Test","abilities":[{"id":"str","name":"Str"},{"id":"dex","name":"Dex"},{"id":"con","name":"Con"},{"id":"wis","name":"Wis"}],
    "actionsPerTurn":2,"bonusActions":false,
    "conditions":[
        {"id":"guarded","name":"Guarded","modifiers":[{"stat":"ac","value":2}],"duration":2},
        {"id":"marked","name":"Marked","stacking":"longest"},
        {"id":"shaken","name":"Shaken","stacking":"value","maxValue":3,"perValue":true,"decay":1,
            "modifiers":[{"stat":"attack","value":-1},{"stat":"speed","op":"multiply","value":0.5}]},
        {"id":"held","name":"Held","flags":["cantMove","offGuard"],"save":{"ability":"str","dc":-100}},
        {"id":"gripped","name":"Gripped","flags":["cantMove"],"save":{"ability":"str","dc":1000}},
        {"id":"stunned","name":"Stunned","flags":["cantAct"],"ends":["damage"]},
        {"id":"braced","name":"Braced","ends":["turnStart"]},
        {"id":"unseen","name":"Unseen","ends":["attack"]},
        {"id":"asleep","name":"Asleep","flags":["cantAct","cantMove"],"ends":["damage","rest"]},
        {"id":"gone","name":"Gone","removes":["asleep","stunned"]}
    ]})";

yh::Character sheet(const yh::Ruleset& rules, const char* name)
{
    yh::Random random(11);
    yh::Character c = yh::makeRandomCharacter(rules, name, "Fighter", random);
    c.stats.setBase("maxHp", 500); // nobody drops in these fights
    c.hp = 500;
    return c;
}

bool has(const std::vector<std::string>& ids, const char* id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool logged(const yh::Encounter& fight, const char* text)
{
    return std::any_of(fight.log().begin(), fight.log().end(), [&](const std::string& line) { return line.find(text) != std::string::npos; });
}

void files()
{
    std::string error;
    const auto parsed = yh::ConditionDefinition::fromJson(R"({"id":"dazed","name":"Dazed","description":"Slow.","flags":["cantAct"],"duration":3,
        "stacking":"value","maxValue":4,"perValue":true,"decay":1,"ends":["rest","damage"],"save":{"ability":"wis","dc":13},
        "removes":["calm"],"modifiers":[{"stat":"ac","op":"add","value":-2}],"disadvantageOnAttacks":true})", &error);
    CHECK(parsed && error.empty());
    if (!parsed) return;
    CHECK(parsed->hasFlag("cantAct") && !parsed->hasFlag("cantMove") && parsed->duration == 3 && parsed->maxValue == 4 && parsed->perValue);
    CHECK(parsed->stacking == yh::ConditionDefinition::Stacking::Value && parsed->decay == 1 && parsed->endsOn("rest") && !parsed->endsOn("attack"));
    CHECK(parsed->saveAbility == "wis" && parsed->saveDc == 13 && parsed->removes.size() == 1 && parsed->disadvantageOnAttacks);
    CHECK(parsed->modifiers.size() == 1 && parsed->modifiers[0].stat == "ac" && parsed->modifiers[0].value == -2);
    const auto again = yh::ConditionDefinition::fromJson(parsed->toJson());
    CHECK(again && again->toJson() == parsed->toJson());

    // Only an id is needed; everything else has a plain default.
    const auto bare = yh::ConditionDefinition::fromJson(R"({"id":"odd"})");
    CHECK(bare && bare->name == "odd" && bare->duration == -1 && bare->stacking == yh::ConditionDefinition::Stacking::Refresh
        && bare->flags.empty() && bare->ends.empty() && bare->saveAbility.empty());

    CHECK(!yh::ConditionDefinition::fromJson(R"({"name":"No id"})"));
    CHECK(!yh::ConditionDefinition::fromJson(R"({"id":"x","ends":["bedtime"]})", &error) && error.find("bedtime") != std::string::npos);
    CHECK(!yh::ConditionDefinition::fromJson(R"({"id":"x","stacking":"pile"})"));
    CHECK(!yh::ConditionDefinition::fromJson(R"({"id":"x","duration":0})"));
    CHECK(!yh::ConditionDefinition::fromJson(R"({"id":"x","maxValue":0})"));
    CHECK(!yh::ConditionDefinition::fromJson(R"({"id":"x","flags":["a","a"]})"));
    CHECK(!yh::ConditionDefinition::fromJson(R"({"id":"x","save":{"dc":12}})"));
    CHECK(!yh::ConditionDefinition::fromJson(R"({"id":"x","modifiers":[{"stat":"ac","op":"square","value":1}]})"));

    const auto rules = yh::Ruleset::fromJson(authored, &error);
    CHECK(rules && rules->conditions.size() == 10 && rules->condition("shaken")->maxValue == 3);
    const auto round = rules ? yh::Ruleset::fromJson(rules->toJson()) : std::nullopt;
    CHECK(round && round->toJson() == rules->toJson());
    const std::string head = R"({"id":"t","name":"T","initiativeAbility":"","armorClassAbility":"","hitDieAbility":"","abilities":[{"id":"str","name":"Str"}],"conditions":)";
    CHECK(yh::Ruleset::fromJson(head + R"([{"id":"a","removes":["b"]},{"id":"b","save":{"ability":"str"}}]})"));
    CHECK(!yh::Ruleset::fromJson(head + R"([{"id":"a","removes":["c"]}]})"));
    CHECK(!yh::Ruleset::fromJson(head + R"([{"id":"a","save":{"ability":"luck"}}]})"));
    CHECK(!yh::Ruleset::fromJson(head + R"([{"id":"a"},{"id":"a"}]})"));

    // One file per condition, named after its id, on top of whatever the ruleset already has.
    const fs::path root = fs::temp_directory_path() / "yorehold-condition-test";
    fs::remove_all(root);
    auto write = [&](const char* file, const char* text) {
        fs::create_directories((root / file).parent_path());
        std::ofstream(root / file, std::ios::binary) << text;
    };
    write("good/conditions/dazed.json", R"({"id":"dazed","name":"Dazed","flags":["cantAct"],"removes":["guarded"]})");
    write("good/conditions/guarded.json", R"({"id":"guarded","name":"On guard","modifiers":[{"stat":"ac","value":1}]})");
    write("good/conditions/notes.txt", "not a condition");
    write("misnamed/conditions/dazed.json", R"({"id":"dizzy"})");
    write("broken/conditions/dazed.json", R"({"id":"dazed","ends":["never"]})");
    write("dangling/conditions/dazed.json", R"({"id":"dazed","removes":["nothing"]})");
    yh::FileSystem disk;
    CHECK(disk.mountFolder(root.string(), "test"));
    if (rules)
    {
        yh::Ruleset loaded = *rules;
        CHECK(loaded.loadConditions(disk, "good/conditions", &error) && error.empty());
        CHECK(loaded.conditions.size() == 11 && loaded.condition("dazed") && loaded.condition("guarded")->name == "On guard"
            && loaded.condition("guarded")->modifiers[0].value == 1);
        const std::string before = loaded.toJson();
        CHECK(!loaded.loadConditions(disk, "misnamed/conditions", &error) && error.find("misnamed/conditions/dazed.json") != std::string::npos);
        CHECK(!loaded.loadConditions(disk, "broken/conditions", &error) && error.find("never") != std::string::npos);
        CHECK(!loaded.loadConditions(disk, "dangling/conditions", &error) && error.find("nothing") != std::string::npos);
        CHECK(loaded.toJson() == before); // a failed load changes nothing
        CHECK(loaded.loadConditions(disk, "missing/conditions") && loaded.toJson() == before); // no folder, no conditions
    }
    disk.unmount("test");
    fs::remove_all(root);
}

void applying(const yh::Ruleset& rules)
{
    yh::Character c = sheet(rules, "subject");
    const int ac = c.armorClass(rules);
    const int attack = c.attackModifier(rules);
    const int speed = c.speedFeet();

    // Applying: modifiers go on with it and come off with it; the duration is the definition's unless one is given.
    c.addCondition(rules, "guarded");
    CHECK(c.hasCondition("guarded") && c.conditionValue("guarded") == 1 && c.armorClass(rules) == ac + 2 && c.conditions[0].roundsLeft == 2);
    c.removeCondition("guarded");
    CHECK(!c.hasCondition("guarded") && c.conditionValue("guarded") == 0 && c.armorClass(rules) == ac);
    c.addCondition(rules, "guarded", 5);
    CHECK(c.conditions[0].roundsLeft == 5);
    c.addCondition(rules, "homebrew", 1); // not in the ruleset: still tracked, with nothing attached
    CHECK(c.hasCondition("homebrew") && !c.hasFlag(rules, "cantAct"));
    c.addCondition(rules, "held");
    CHECK(c.hasFlag(rules, "cantMove") && c.hasFlag(rules, "offGuard") && !c.hasFlag(rules, "cantAct"));
    c.removeCondition("held");
    CHECK(!c.hasFlag(rules, "cantMove"));

    // Stacking. Refresh: the newest duration stands, and the modifiers are not doubled.
    c.addCondition(rules, "guarded", 1);
    CHECK(c.conditions.size() == 2 && c.conditions.back().roundsLeft == 1 && c.armorClass(rules) == ac + 2);
    // Longest: a shorter one does not cut a longer one short, and nothing outlasts "until removed".
    c.addCondition(rules, "marked", 4);
    c.addCondition(rules, "marked", 2);
    CHECK(c.conditions.back().roundsLeft == 4);
    c.addCondition(rules, "marked", 6);
    CHECK(c.conditions.back().roundsLeft == 6);
    c.addCondition(rules, "marked");
    c.addCondition(rules, "marked", 3);
    CHECK(c.conditions.back().roundsLeft == -1);
    // Value: they add up to the cap, and additive modifiers scale with the value.
    c.addCondition(rules, "shaken");
    CHECK(c.conditionValue("shaken") == 1 && c.attackModifier(rules) == attack - 1 && c.speedFeet() == speed / 2);
    c.addCondition(rules, "shaken");
    CHECK(c.conditionValue("shaken") == 2 && c.attackModifier(rules) == attack - 2 && c.speedFeet() == speed / 2);
    c.addCondition(rules, "shaken", yh::Character::definedDuration, 5);
    CHECK(c.conditionValue("shaken") == 3 && c.attackModifier(rules) == attack - 3);

    // A saved sheet keeps the value and its modifiers.
    const auto restored = yh::Character::fromJson(c.toJson());
    CHECK(restored && restored->conditionValue("shaken") == 3 && restored->attackModifier(rules) == attack - 3 && restored->toJson() == c.toJson());
    const auto older = yh::Character::fromJson(R"({"version":1,"conditions":[{"id":"shaken","roundsLeft":2}]})");
    CHECK(older && older->conditionValue("shaken") == 1); // sheets from before values

    // Ending by time and by decay: one round takes a point off Shaken and ends the one-round conditions.
    std::vector<std::string> ended = c.endRound(rules);
    CHECK(has(ended, "guarded") && has(ended, "homebrew") && ended.size() == 2 && c.armorClass(rules) == ac);
    CHECK(c.conditionValue("shaken") == 2 && c.attackModifier(rules) == attack - 2 && c.hasCondition("marked"));
    ended = c.endRound(rules);
    CHECK(ended.empty() && c.conditionValue("shaken") == 1 && c.attackModifier(rules) == attack - 1);
    ended = c.endRound(rules);
    CHECK(has(ended, "shaken") && !c.hasCondition("shaken") && c.attackModifier(rules) == attack && c.speedFeet() == speed);
    c.removeCondition("marked");
    CHECK(c.conditions.empty());

    // Ending by a save: rolled at the end of the round, and only when there are dice to roll.
    yh::Random random(5);
    c.addCondition(rules, "held");    // DC so low it always ends
    c.addCondition(rules, "gripped"); // DC so high it never does
    CHECK(c.endRound(rules).empty() && c.hasCondition("held"));
    ended = c.endRound(rules, &random);
    CHECK(has(ended, "held") && ended.size() == 1 && c.hasCondition("gripped") && c.hasFlag(rules, "cantMove"));
    c.removeCondition("gripped");

    // Ending by what happens: only the conditions that listen for it.
    c.addCondition(rules, "asleep");
    c.addCondition(rules, "braced");
    c.addCondition(rules, "stunned");
    CHECK(c.conditionEvent(rules, "move").empty() && c.conditions.size() == 3);
    ended = c.conditionEvent(rules, "rest");
    CHECK(ended.size() == 1 && has(ended, "asleep") && c.hasCondition("stunned"));
    c.addCondition(rules, "asleep");
    ended = c.conditionEvent(rules, "damage");
    CHECK(ended.size() == 2 && has(ended, "asleep") && has(ended, "stunned") && c.hasCondition("braced") && !c.hasFlag(rules, "cantAct"));

    // One condition can take others off as it goes on.
    c.addCondition(rules, "asleep");
    c.addCondition(rules, "stunned");
    c.addCondition(rules, "gone");
    CHECK(c.hasCondition("gone") && !c.hasCondition("asleep") && !c.hasCondition("stunned") && c.hasCondition("braced"));

    // The plain countdown still works for sheets used without a ruleset.
    yh::Character plain = sheet(rules, "plain");
    plain.addCondition(rules, "guarded", 1);
    plain.endRound();
    CHECK(!plain.hasCondition("guarded") && plain.armorClass(rules) == ac);
}

void inAFight(const yh::Ruleset& rules)
{
    yh::Character a = sheet(rules, "Ada");
    yh::Character b = sheet(rules, "Bek");
    yh::Encounter fight(rules, 21);
    fight.add(a, 0);
    fight.add(b, 1);
    b.addCondition(rules, "braced");
    a.addCondition(rules, "braced");
    fight.start();
    yh::Character* first = fight.current().character;
    yh::Character* second = first == &a ? &b : &a;
    const size_t secondIndex = 1 - fight.currentIndex();

    // The start of a turn ends what lasts until then, for whoever's turn it is.
    CHECK(!first->hasCondition("braced") && second->hasCondition("braced") && logged(fight, "is no longer Braced"));
    CHECK(fight.current().budget.actions == 2 && fight.current().budget.movementLeft > 0);

    // Attacking ends what ends on an attack; being hit ends what ends on damage.
    first->addCondition(rules, "unseen");
    second->addCondition(rules, "stunned");
    second->addCondition(rules, "held");
    yh::AttackResult swing;
    for (int tries = 0; tries < 40 && !swing.hit; tries++)
    {
        while (fight.current().character != first)
            fight.nextTurn();
        if (!fight.canStrike())
        {
            // Its actions are spent: round to its next turn. The other side is stunned and does nothing.
            fight.nextTurn();
            continue;
        }
        const bool stunnedBefore = second->hasCondition("stunned");
        swing = fight.attack(secondIndex);
        CHECK(!first->hasCondition("unseen"));
        CHECK(swing.hit ? !second->hasCondition("stunned") : second->hasCondition("stunned") == stunnedBefore);
    }
    CHECK(swing.hit && logged(fight, "is no longer Unseen") && logged(fight, "is no longer Stunned"));

    // "cantAct" and "cantMove" take the turn's actions and movement; the round's end rolls the saves.
    second->addCondition(rules, "stunned");
    second->addCondition(rules, "gripped");
    while (fight.current().character != second)
        fight.nextTurn();
    CHECK(fight.current().budget.actions == 0 && !fight.current().budget.reaction && fight.current().budget.movementLeft == 0);
    CHECK(!fight.canAct() && !fight.canStrike() && !fight.dash() && !fight.spendMovement(1));
    second->removeCondition("stunned");
    second->addCondition(rules, "held");
    const int round = fight.round();
    while (fight.round() == round)
        fight.nextTurn();
    CHECK(!second->hasCondition("held") && second->hasCondition("gripped") && logged(fight, "is no longer Held"));
    while (fight.current().character != second)
        fight.nextTurn();
    CHECK(fight.current().budget.actions == 2 && fight.current().budget.movementLeft == 0); // still gripped
}

}

void conditions()
{
    files();
    const auto rules = yh::Ruleset::fromJson(authored);
    CHECK(rules.has_value());
    if (!rules) return;
    applying(*rules);
    inAFight(*rules);
}

}
