#include "ChoicesTests.h"
#include "Checks.h"

#include <yorehold/framework/rpg/CharacterChoices.h>
#include <yorehold/framework/rpg/Compendium.h>
#include <yorehold/framework/assets/FileSystem.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

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

void options()
{
    namespace fs = std::filesystem;
    std::string error;
    // Strict files: a typo is refused with its field named.
    CHECK(yh::raceFromJson(R"({"id":"elf","speed":35,"darkvision":60,"abilities":{"dex":2},"feats":["keen"]})", &error) && error.empty());
    CHECK(!yh::raceFromJson(R"({"id":"elf","sped":35})", &error) && error == "sped: unknown field");
    CHECK(!yh::raceFromJson(R"({"id":"Elf"})", &error) && error.find("id") == 0);
    CHECK(!yh::backgroundFromJson(R"({"id":"sailor","items":["Rope"]})", &error) && error.find("items") == 0);
    CHECK(!yh::featFromJson(R"({"id":"x","kind":"epic"})", &error) && error.find("kind") == 0);
    CHECK(!yh::featFromJson(R"({"id":"x","requires":{"lvl":2}})", &error) && error == "requires.lvl: unknown field");
    CHECK(!yh::featFromJson(R"({"id":"x","modifiers":[{"stat":"ac","value":"one"}]})", &error) && error.find("modifiers[0].value") == 0);
    CHECK(!yh::featFromJson(R"({"id":"x","resources":{"luck":0}})", &error) && error.find("resources.luck") == 0);
    const auto tough = yh::featFromJson(R"({"id":"tough","kind":"general","modifiers":[{"stat":"maxHp","value":3}],
        "resources":{"grit":1},"requires":{"level":2,"abilities":{"con":12}}})", &error);
    CHECK(tough && tough->kind == "general" && tough->needs.level == 2 && tough->needs.abilities.at("con") == 12
        && tough->gives.modifiers.size() == 1 && tough->gives.resources.at("grit").max == 1);

    // A ruleset folder of options; references are checked once everything is read.
    const fs::path root = fs::temp_directory_path() / "yorehold-options-test";
    fs::remove_all(root);
    for (const char* folder : {"good/feats", "good/races", "good/backgrounds", "bad/races"})
        fs::create_directories(root / folder);
    std::ofstream(root / "good/feats/keen.json") << R"({"id":"keen","kind":"race","ranks":{"perception":"expert"}})";
    std::ofstream(root / "good/feats/tough.json") << R"({"id":"tough","modifiers":[{"stat":"maxHp","value":3}],"resources":{"grit":1},
        "requires":{"level":2,"abilities":{"con":12}}})";
    std::ofstream(root / "good/feats/sure-foot.json") << R"({"id":"sure-foot","kind":"class","proficiencies":["acrobatics"],
        "requires":{"classes":["scout"],"races":["elf"],"proficiencies":["stealth"]}})";
    std::ofstream(root / "good/races/elf.json") << R"({"id":"elf","name":"Elf","speed":35,"darkvision":60,"abilities":{"dex":2,"con":-2},"feats":["keen"]})";
    std::ofstream(root / "good/backgrounds/sailor.json") << R"({"id":"sailor","proficiencies":["athletics","survival"],"items":["sword"],"abilities":{"str":1}})";
    std::ofstream(root / "bad/races/orc.json") << R"({"id":"orc","feats":["rage"]})";
    yh::FileSystem disk;
    CHECK(disk.mountFolder(root.string(), "test"));
    auto compendium = classes();
    CHECK(compendium.loadOptions(disk, "good", &error) && error.empty());
    CHECK(compendium.feats.size() == 3 && compendium.race("elf") && compendium.background("sailor"));
    CHECK(!compendium.loadOptions(disk, "bad", &error) && error.find("orc.json") != std::string::npos
        && error.find("no feat \"rage\"") != std::string::npos && !compendium.race("orc") && compendium.race("elf"));
    disk.unmount("test");
    fs::remove_all(root);

    // The race and background shape the sheet.
    const auto rules = yh::Ruleset::modern();
    auto choices = ana(rules);
    choices.race = "elf";
    choices.background = "sailor";
    const auto elf = compendium.build(rules, choices, &error);
    CHECK(elf && error.empty());
    if (!elf) return;
    CHECK(elf->ancestry == "Elf" && elf->abilityScore("dex") == 12 && elf->abilityScore("con") == 12 && elf->abilityScore("str") == 11);
    CHECK(elf->speedFeet() == 35 && elf->stats.integer("darkvision") == 60 && elf->maxHp() == 10 + 2 + 1);
    CHECK(elf->proficiencies.contains("survival") && elf->proficiencyRank(rules, "perception") == "expert");
    CHECK(elf->inventory.size() == 2);

    // Feat picks: requirements judged at the level they were taken.
    auto picked = choices;
    picked.levels[0].picks["feats"] = {"tough"};
    CHECK(!compendium.build(rules, picked, &error) && error == "levels[0].picks.feats: \"tough\" needs level 2");
    picked.levels[0].picks.clear();
    picked.levels.push_back({"soldier", {{"feats", {"tough"}}}});
    const auto second = compendium.build(rules, picked, &error);
    CHECK(second && second->maxHp() == 13 + 7 + 3 && second->resources.at("grit").max == 1);
    auto weak = picked; weak.scores["con"] = 12; // 10 after the elf's -2
    CHECK(!compendium.build(rules, weak, &error) && error == "levels[1].picks.feats: \"tough\" needs con 12");
    auto twice = picked; twice.levels.push_back({"soldier", {{"feats", {"tough"}}}});
    CHECK(!compendium.build(rules, twice, &error) && error == "levels[2].picks.feats: \"tough\" is already taken");
    auto foot = choices;
    foot.levels.push_back({"scout", {{"feats", {"sure-foot"}}}});
    CHECK(!compendium.build(rules, foot, &error) && error == "levels[1].picks.feats: \"sure-foot\" needs training in stealth");
    foot.levels[1].picks["skills"] = {"stealth"};
    CHECK(compendium.build(rules, foot, &error) && error.empty());
    auto human = foot; human.race.clear();
    CHECK(!compendium.build(rules, human, &error) && error == "levels[1].picks.feats: \"sure-foot\" is for another race");
    auto soldierOnly = foot; soldierOnly.levels[1].classId = "soldier";
    CHECK(!compendium.build(rules, soldierOnly, &error) && error == "levels[1].picks.feats: \"sure-foot\" is for another class");
    auto missing = choices; missing.levels[0].picks["feats"] = {"flight"};
    CHECK(!compendium.build(rules, missing, &error) && error == "levels[0].picks.feats: no feat \"flight\"");
    auto noRace = choices; noRace.race = "giant";
    CHECK(!compendium.build(rules, noRace, &error) && error == "race: no race \"giant\"");
    auto noBackground = choices; noBackground.background = "pirate";
    CHECK(!compendium.build(rules, noBackground, &error) && error == "background: no background \"pirate\"");

    // A rebuild swaps feat modifiers for the new set and leaves others alone.
    auto live = *second;
    live.addModifier("blessed", {"maxHp", yh::Modifier::Op::Add, 1, ""}, 3);
    live.adoptBuild(*compendium.build(rules, picked));
    CHECK(live.maxHp() == 23 + 1);
    live.adoptBuild(*compendium.build(rules, choices));
    CHECK(live.maxHp() == 13 + 1 && live.hasCondition("blessed"));
}

void levelTables()
{
    std::string error;
    // Strict rows, named by index.
    CHECK(!yh::classLevelsFromJson(R"([{}, {"feat":["class"]}])", &error) && error == "levels[1].feat: unknown field");
    CHECK(!yh::classLevelsFromJson(R"([{"feats":["epic"]}])", &error) && error.find("levels[0].feats") == 0);
    CHECK(!yh::classLevelsFromJson(R"([{"slots":{"0":2}}])", &error) && error.find("levels[0].slots.0") == 0);
    CHECK(!yh::classLevelsFromJson(R"([{"features":[{"id":"x","modifier":[]}]}])", &error) && error == "levels[0].features[0].modifier: unknown field");
    CHECK(!yh::Compendium::classFromJson(R"({"id":"x","levels":{}})", &error) && error.find("levels") == 0);

    auto compendium = classes();
    const auto mage = yh::Compendium::classFromJson(R"({"id":"mage","name":"Mage","hitDie":6,"proficiencyRanks":{"dc":"trained"},"levels":[
        {"features":[{"id":"spellbook","name":"Spellbook","resources":{"recovery":1}}], "slots":{"1":2}, "skills":1},
        {"feats":["class"], "slots":{"1":3}},
        {"ranks":{"dc":"expert"}, "slots":{"1":4,"2":2}, "feats":["general","skill"],
         "features":[{"id":"focus","modifiers":[{"stat":"dc","value":1}]}]}]})", &error);
    CHECK(mage && error.empty() && mage->levels.size() == 3 && mage->levels[2].slots.at(2) == 2);
    if (!mage) return;
    // The table survives a round trip.
    const auto again = yh::Compendium::classFromJson(yh::Compendium::classToJson(*mage), &error);
    CHECK(again && yh::Compendium::classToJson(*again) == yh::Compendium::classToJson(*mage));
    compendium.classes["mage"] = *mage;
    compendium.feats["arcane-eye"] = *yh::featFromJson(R"({"id":"arcane-eye","kind":"class","requires":{"classes":["mage"]}})");
    compendium.feats["sturdy"] = *yh::featFromJson(R"({"id":"sturdy","kind":"general","modifiers":[{"stat":"maxHp","value":2}]})");
    compendium.feats["lore"] = *yh::featFromJson(R"({"id":"lore","kind":"skill"})");

    const auto rules = yh::Ruleset::modern();
    auto choices = ana(rules);
    choices.levels = {{"mage", {{"skills", {"arcana"}}}}};
    const auto first = compendium.build(rules, choices, &error);
    CHECK(first && error.empty() && first->resources.at("slots-1").max == 2 && first->resources.at("recovery").max == 1
        && first->proficiencies.contains("arcana"));

    // Each row in turn: slots replace the last row's, ranks rise, features stay.
    choices.levels.push_back({"mage", {{"feats", {"arcane-eye"}}}});
    choices.levels.push_back({"mage", {{"feats", {"sturdy", "lore"}}}});
    const auto third = compendium.build(rules, choices, &error);
    CHECK(third && error.empty() && third->resources.at("slots-1").max == 4 && third->resources.at("slots-2").max == 2);
    CHECK(third && third->proficiencyRank(rules, "dc") == "expert" && third->stats.integer("dc") == 1 && third->resources.at("recovery").max == 1);
    CHECK(third && third->maxHp() == (6 + 2) + 2 * (4 + 2) + 2);

    // Picks must fit what the row offers.
    auto extraSkill = choices; extraSkill.levels[0].picks["skills"] = {"arcana", "stealth"};
    CHECK(!compendium.build(rules, extraSkill, &error) && error == "levels[0].picks.skills: 2 picked, this level offers 1");
    auto wrongKind = choices; wrongKind.levels[1].picks["feats"] = {"sturdy"};
    CHECK(!compendium.build(rules, wrongKind, &error)
        && error == "levels[1].picks.feats: \"sturdy\" is a general feat, and this level has no general feat to choose");
    auto tooMany = choices; tooMany.levels[2].picks["feats"] = {"sturdy", "lore", "arcane-eye"};
    CHECK(!compendium.build(rules, tooMany, &error) && error.find("levels[2].picks.feats: \"arcane-eye\" is a class feat") == 0);
    auto open = choices; open.levels[2].picks.clear();
    CHECK(compendium.build(rules, open, &error) && error.empty()); // a choice left open builds; it's asked for later

    // Any level into any class: the class's own level picks its row, and slots don't stack.
    auto multi = choices;
    multi.levels.insert(multi.levels.begin() + 1, yh::LevelChoice{"soldier", {}});
    const auto mixed = compendium.build(rules, multi, &error);
    CHECK(mixed && error.empty() && mixed->level == 4 && mixed->characterClass == "Mage / Soldier");
    CHECK(mixed && mixed->resources.at("slots-1").max == 4 && mixed->maxHp() == 22 + 8);
    auto twoCasters = choices;
    compendium.classes["mage2"] = *mage; compendium.classes["mage2"].id = "mage2";
    twoCasters.levels.push_back({"mage2", {}});
    const auto both = compendium.build(rules, twoCasters, &error);
    CHECK(both && both->resources.at("slots-1").max == 4 && both->resources.at("recovery").max == 2);
}

void hands()
{
    yh::Character c;
    auto item = [](const char* id, const char* slot, int hands) {
        yh::Item i; i.id = id; i.name = id; i.slot = slot; i.hands = hands;
        return i;
    };
    c.inventory = {item("sword", "mainHand", 1), item("shield", "offHand", 1), item("greatsword", "mainHand", 2), item("mail", "armor", 1)};
    c.inventory[1].modifiers.push_back({"ac", yh::Modifier::Op::Add, 2, ""});
    c.stats.setBase("ac", 10);
    CHECK(c.equip(0) && c.equip(1) && c.equip(3) && c.handsInUse() == 2 && c.stats.integer("ac") == 12);
    // Both hands on the greatsword: the sword leaves its slot and the shield its hand. Armour stays.
    CHECK(c.equip(2) && c.handsInUse() == 2 && !c.inventory[0].equipped && !c.inventory[1].equipped && c.inventory[3].equipped
        && c.stats.integer("ac") == 10);
    // Taking the shield back up puts the greatsword away.
    CHECK(c.equip(1) && !c.inventory[2].equipped && c.handsInUse() == 1 && !c.weapon());
    CHECK(c.equip(0) && c.handsInUse() == 2 && c.inventory[1].equipped);
}

void loot()
{
    std::string error;
    const auto table = yh::LootTable::fromJson(
        R"({"coins":"2d6","items":["sword",{"item":"sword","chance":0,"quantity":3},{"item":"sword","chance":1,"quantity":2}]})", &error);
    CHECK(table && table->coins == "2d6" && table->items.size() == 3 && table->items[1].chance == 0);
    if (!table) return;
    yh::Random a(3), b(3);
    const auto found = yh::rollLoot(*table, a);
    CHECK(found.coins >= 2 && found.coins <= 12 && found.items.size() == 2 && found.items[1].second == 2);
    CHECK(yh::rollLoot(*table, b).coins == found.coins);
    const auto back = yh::LootTable::fromJson(table->toJson());
    CHECK(back && back->toJson() == table->toJson());
    CHECK(yh::LootTable::fromJson(R"({"coins":40})")->coins == "40" && yh::LootTable::fromJson("{}")->empty());
    CHECK(!yh::LootTable::fromJson(R"({"coins":"lots"})", &error) && error.find("loot.coins") == 0);
    CHECK(!yh::LootTable::fromJson(R"({"items":[{"item":"sword","chance":2}]})", &error) && error.find("loot.items[0].chance") == 0);
    CHECK(!yh::LootTable::fromJson(R"({"gems":1})", &error) && error == "loot.gems: unknown field");

    const auto compendium = classes();
    CHECK(compendium.checkLoot(*table) && compendium.lootItems(found).size() == 2 && compendium.lootItems(found)[1].quantity == 2);
    auto missing = *table;
    missing.items.push_back({"gem", 1, 1});
    CHECK(!compendium.checkLoot(missing, &error) && error.find("gem") != std::string::npos);

    // Creature files carry a table, and sheets carry coins.
    const auto creature = yh::Compendium::creatureFromJson(R"({"id":"rat","loot":{"coins":"1d4"}})", &error);
    CHECK(creature && creature->loot.coins == "1d4"
        && yh::Compendium::creatureFromJson(yh::Compendium::creatureToJson(*creature))->loot.coins == "1d4");
    yh::Character rich;
    rich.coins = 125;
    CHECK(yh::Character::fromJson(rich.toJson())->coins == 125);
}

void scoreMethods()
{
    auto rules = yh::Ruleset::modern();
    std::string error;
    auto bought = ana(rules); // five 10s and a 14: 5 * 2 + 7 points
    bought.scoreMethod = "pointBuy";
    CHECK(yh::pointBuyCost(rules, bought.scores) == 17 && bought.check(rules, &error));
    bought.scores["str"] = 15; bought.scores["dex"] = 15; // 17 - 4 + 18
    CHECK(yh::pointBuyCost(rules, bought.scores) == 31 && !bought.check(rules, &error) && error == "scores: cost 31 points, the budget is 27");
    bought.scores["dex"] = 16;
    CHECK(yh::pointBuyCost(rules, bought.scores) == -1 && !bought.check(rules, &error) && error == "scores: point buy only buys scores 8 to 15");

    auto arrayed = ana(rules);
    arrayed.scoreMethod = "array";
    CHECK(!arrayed.check(rules, &error) && error == "scores: the standard array uses each of its values once");
    const int values[] = {8, 15, 13, 14, 10, 12};
    for (size_t i = 0; i < rules.abilities.size(); i++)
        arrayed.scores[rules.abilities[i].id] = values[i];
    CHECK(arrayed.check(rules, &error));

    // The numbers are data and survive the trip through JSON.
    rules.scoreMethods.roll = "3d6";
    rules.scoreMethods.standardArray = {14, 13, 12, 11, 10, 9};
    rules.scoreMethods.pointBudget = 20;
    rules.scoreMethods.pointCosts = {{7, 0}, {12, 5}};
    const auto back = yh::Ruleset::fromJson(rules.toJson(), &error);
    CHECK(back && back->scoreMethods.roll == "3d6" && back->scoreMethods.standardArray == rules.scoreMethods.standardArray
        && back->scoreMethods.pointBudget == 20 && back->scoreMethods.pointCosts == rules.scoreMethods.pointCosts);
    auto j = nlohmann::json::parse(rules.toJson());
    j["scoreMethods"]["standardArray"] = {15, 40};
    CHECK(!yh::Ruleset::fromJson(j.dump(), &error) && error.find("scoreMethods") != std::string::npos);
    j["scoreMethods"]["standardArray"] = {15, 14};
    const auto short_ = yh::Ruleset::fromJson(j.dump(), &error);
    arrayed.scores = {{"str", 15}, {"dex", 14}, {"con", 10}, {"int", 10}, {"wis", 10}, {"cha", 10}};
    CHECK(short_ && !arrayed.check(*short_, &error));
    j = nlohmann::json::parse(rules.toJson());
    j["scoreMethods"]["roll"] = "lots";
    CHECK(!yh::Ruleset::fromJson(j.dump(), &error));
}

}

void characterChoices()
{
    files();
    scoreMethods();
    hands();
    loot();
    building();
    liveState();
    options();
    levelTables();
}

}
