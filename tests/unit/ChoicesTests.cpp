#include "ChoicesTests.h"
#include "Checks.h"

#include <yorehold/framework/rpg/CharacterChoices.h>
#include <yorehold/framework/rpg/Compendium.h>
#include <nlohmann/json.hpp>

namespace regression
{
namespace
{

yh::Compendium classes()
{
    yh::Compendium compendium;
    yh::Item sword; sword.id = "sword"; sword.name = "Sword"; sword.slot = "mainHand"; sword.damage = "1d8";
    sword.modifiers.push_back({"attack", yh::Modifier::Op::Add, 1, ""});
    compendium.items[sword.id] = sword;
    yh::ClassDefinition soldier; soldier.id = "soldier"; soldier.name = "Soldier"; soldier.hitDie = 10; soldier.bonusHp = 2;
    soldier.speed = 25; soldier.proficiencies = {"athletics", "weapons"}; soldier.items = {"sword"};
    soldier.resources["second-wind"] = {1, 1};
    compendium.classes[soldier.id] = soldier;
    yh::ClassDefinition scout; scout.id = "scout"; scout.name = "Scout"; scout.hitDie = 8;
    compendium.classes[scout.id] = scout;
    return compendium;
}

yh::CharacterChoices ana(const yh::Ruleset& rules)
{
    yh::CharacterChoices c;
    c.name = "Ana";
    for (const auto& ability : rules.abilities)
        c.scores[ability.id] = 10;
    c.scores["con"] = 14;
    c.levels.push_back({"soldier", {}});
    return c;
}

void files()
{
    const auto rules = yh::Ruleset::modern();
    auto choices = ana(rules);
    choices.race = "elf"; choices.background = "sailor"; choices.scoreMethod = "pointBuy"; choices.xp = 120;
    choices.ruleset = "test"; choices.notes = "Keeps a ledger.";
    choices.levels.push_back({"scout", {{"skills", {"stealth"}}, {"feats", {"quick"}}}});
    std::string error;
    const auto back = yh::CharacterChoices::fromJson(choices.toJson(), &error);
    CHECK(back && error.empty() && back->toJson() == choices.toJson() && back->level() == 2);
    CHECK(back && back->levels[1].picks.at("feats") == std::vector<std::string>{"quick"} && back->scoreMethod == "pointBuy");

    // Every mistake names its field.
    auto broken = [&](nlohmann::json j, const char* field) {
        std::string problem;
        const bool refused = !yh::CharacterChoices::fromJson(j.dump(), &problem);
        return refused && problem.find(field) == 0;
    };
    const auto good = nlohmann::json::parse(choices.toJson());
    auto j = good; j["scores"]["str"] = 31; CHECK(broken(j, "scores.str"));
    j = good; j["scores"]["str"] = "ten"; CHECK(broken(j, "scores.str"));
    j = good; j.erase("scores"); CHECK(broken(j, "scores"));
    j = good; j["levels"] = nlohmann::json::array(); CHECK(broken(j, "levels"));
    j = good; j["levels"][0].erase("class"); CHECK(broken(j, "levels[0].class"));
    j = good; j["levels"][1]["picks"]["feats"] = nlohmann::json::array({""}); CHECK(broken(j, "levels[1].picks.feats"));
    j = good; j["levels"][0]["subclass"] = "x"; CHECK(broken(j, "levels[0].subclass"));
    j = good; j["scoreMethod"] = "dream"; CHECK(broken(j, "scoreMethod"));
    j = good; j["xp"] = -1; CHECK(broken(j, "xp"));
    j = good; j["version"] = 2; CHECK(broken(j, "version"));
    j = good; j["colour"] = "red"; CHECK(broken(j, "colour"));
    std::string problem;
    CHECK(!yh::CharacterChoices::fromJson("{", &problem) && !problem.empty());

    // The ruleset decides which abilities there are.
    CHECK(choices.check(rules, &error));
    auto missing = choices; missing.scores.erase("wis");
    CHECK(!missing.check(rules, &error) && error == "scores.wis: missing");
    auto extra = choices; extra.scores["luck"] = 12;
    CHECK(!extra.check(rules, &error) && error == "scores.luck: not an ability of this ruleset");
}

void building()
{
    const auto rules = yh::Ruleset::modern();
    const auto compendium = classes();
    std::string error;
    auto choices = ana(rules);
    const auto first = compendium.build(rules, choices, &error);
    CHECK(first && error.empty());
    if (!first) return;
    CHECK(first->name == "Ana" && first->level == 1 && first->characterClass == "Soldier" && first->hitDie == "1d10");
    CHECK(first->maxHp() == 10 + 2 + 2 && first->hp == first->maxHp() && first->speedFeet() == 25);
    CHECK(first->abilityScore("con") == 14 && first->proficiencies.contains("athletics"));
    CHECK(first->weapon() && first->weapon()->id == "sword" && first->resources.at("second-wind").max == 1);

    // Later levels add their class's average hit die (rounded up) plus CON; skill picks train.
    choices.levels.push_back({"scout", {{"skills", {"stealth"}}}});
    choices.levels.push_back({"soldier", {}});
    const auto third = compendium.build(rules, choices, &error);
    CHECK(third && third->level == 3 && third->maxHp() == 14 + (5 + 2) + (6 + 2));
    CHECK(third && third->characterClass == "Soldier / Scout" && third->proficiencies.contains("stealth"));

    // A weak constitution still gains at least 1 HP a level.
    auto frail = choices; frail.scores["con"] = 1;
    CHECK(compendium.build(rules, frail)->maxHp() == std::max(1, 12 - 5) + 1 + std::max(1, 6 - 5));

    auto unknown = choices; unknown.levels[1].classId = "bard";
    CHECK(!compendium.build(rules, unknown, &error) && error == "levels[1].class: no class \"bard\"");
    auto noScores = choices; noScores.scores.clear();
    CHECK(!compendium.build(rules, noScores, &error) && error.find("scores.") == 0);

    // A rolled character is the same sheet whether made directly or from its choices.
    yh::Random a(42), b(42);
    const auto made = compendium.makeCharacter(rules, "soldier", "Bo", a);
    const auto rolled = yh::rollChoices(rules, "Bo", "soldier", b);
    CHECK(made && rolled.scoreMethod == "roll" && rolled.level() == 1 && made->toJson() == compendium.build(rules, rolled)->toJson());
    yh::Random classic(7);
    for (const auto& [ability, score] : yh::rollChoices(yh::Ruleset::classic(), "Cy", "soldier", classic).scores)
        CHECK(score >= 3 && score <= 18);
}

void liveState()
{
    const auto rules = yh::Ruleset::modern();
    const auto compendium = classes();
    auto choices = ana(rules);
    auto live = *compendium.build(rules, choices);
    live.hp = 5; live.tempHp = 3; live.xp = 200;
    live.resources["second-wind"].current = 0;
    live.resources["torch"] = {2, 3};
    live.addModifier("blessed", {"attack", yh::Modifier::Op::Add, 1, ""}, 3);
    yh::Item rope; rope.id = "rope"; rope.name = "Rope";
    live.inventory.push_back(rope);
    const int attack = live.attackModifier(rules);

    // Rebuilding the same choices changes nothing that was lived through.
    const std::string before = live.toJson();
    live.adoptBuild(*compendium.build(rules, choices));
    CHECK(live.toJson() == before);

    // A new level raises the maximum but not the HP lost; the extra modifiers and gear stay.
    choices.levels.push_back({"soldier", {}});
    live.adoptBuild(*compendium.build(rules, choices));
    CHECK(live.level == 2 && live.maxHp() == 14 + 8 && live.hp == 5 && live.tempHp == 3 && live.xp == 200);
    CHECK(live.attackModifier(rules) == attack && live.inventory.size() == 2 && live.hasCondition("blessed"));
    CHECK(live.resources.at("second-wind").current == 0 && live.resources.at("torch").current == 2);

    // Lower scores bring HP down with the maximum.
    live.hp = live.maxHp();
    choices.scores["con"] = 10;
    live.adoptBuild(*compendium.build(rules, choices));
    CHECK(live.maxHp() == 12 + 6 && live.hp == 18);

    // A sheet from before choices: its scores, its level, all in the class it was made as.
    yh::Character old = *compendium.build(rules, ana(rules));
    old.level = 3; old.xp = 900; old.notes = "Old";
    const auto read = yh::choicesFromSheet(rules, old, "soldier");
    CHECK(read.level() == 3 && read.levels[2].classId == "soldier" && read.scores.at("con") == 14 && read.xp == 900);
    CHECK(read.scoreMethod == "fixed" && read.notes == "Old" && read.check(rules));
}

}

void characterChoices()
{
    files();
    building();
    liveState();
}

}
