#include "ProficiencyTests.h"
#include "Checks.h"

#include <yorehold/framework/rpg/Compendium.h>
#include <nlohmann/json.hpp>

namespace regression
{
namespace
{

yh::Ruleset rankedRules()
{
    auto rules = yh::Ruleset::modern();
    rules.proficiencyRanks = {{"none", "None", 0, false}, {"basic", "Basic", 2, true},
        {"advanced", "Advanced", 4, true}, {"master", "Master", 6, true}, {"highest", "Highest", 8, true}};
    rules.proficientRank = "basic";
    rules.untrainedRank = "none";
    return rules;
}

void numbers()
{
    const auto rules = rankedRules();
    yh::Character sheet;
    sheet.stats.setBase("str", 16); sheet.stats.setBase("dex", 14); sheet.stats.setBase("wis", 18);
    sheet.stats.setBase("ac", 10); sheet.stats.setBase("attack", 1); sheet.stats.setBase("dc", 2);
    sheet.dcAbility = "wis";
    for (const auto& rank : rules.proficiencyRanks)
    {
        for (int level = 1; level <= 20; level++)
        {
            sheet.level = level;
            for (const char* target : {"weapons", "armor", "dc", "str", "athletics"})
                sheet.proficiencyRanks[target] = rank.id;
            const int bonus = rank.bonus + (rank.addsLevel ? level : 0);
            CHECK(sheet.attackModifier(rules) == 4 + bonus);
            CHECK(sheet.saveModifier(rules, "str") == 3 + bonus);
            CHECK(sheet.checkModifier(rules, "athletics") == 3 + bonus);
            CHECK(sheet.armorClass(rules) == 12 + bonus);
            CHECK(sheet.difficultyClass(rules) == 16 + bonus);
            CHECK(sheet.difficultyClass(rules, "str") == 15 + bonus);
            CHECK(sheet.checkModifier(rules, "str") == 3 && sheet.initiativeModifier(rules) == 2);
        }
    }
    sheet.level = 7;
    sheet.proficiencyRanks.clear();
    sheet.proficiencies = {"weapons", "armor", "athletics", "str", "dc"};
    CHECK(sheet.proficiencyRank(rules, "athletics") == "basic" && sheet.attackModifier(rules) == 13);
    CHECK(sheet.proficiencyRank(rules, "stealth") == "none" && sheet.checkModifier(rules, "stealth") == 2);
    sheet.proficiencyRanks["weapons"] = "none";
    CHECK(sheet.attackModifier(rules) == 4);
    sheet.proficiencyRanks["armor"] = "advanced";
    CHECK(sheet.armorClass(rules) == 23);
    sheet.stats.addModifier({"ac", yh::Modifier::Op::Add, -2, "test"});
    sheet.stats.addModifier({"dc", yh::Modifier::Op::Add, 1, "test"});
    CHECK(sheet.armorClass(rules) == 21 && sheet.difficultyClass(rules) == 26);
    sheet.dcAbility.clear();
    CHECK(sheet.difficultyClass(rules) == 22);
    auto custom = rules;
    custom.proficiencyRanks[1].bonus = 7; custom.proficiencyRanks[1].addsLevel = false;
    CHECK(sheet.proficiencyModifier(custom, "athletics") == 7);

    // Legacy tables ignore stored rank choices and do not add armour proficiency to AC.
    for (const auto& legacy : {yh::Ruleset::modern(), yh::Ruleset::classic()})
    {
        CHECK(sheet.attackModifier(legacy) == legacy.abilityModifier(16) + legacy.proficiencyBonus(7) + 1);
        CHECK(sheet.saveModifier(legacy, "str") == legacy.abilityModifier(16) + legacy.proficiencyBonus(7));
        CHECK(sheet.armorClass(legacy) == 8 + (legacy.armorClassAbility.empty() ? 0 : legacy.abilityModifier(14)));
        CHECK(sheet.checkProficiencyRanks(legacy));
    }
}

void serialization()
{
    const auto rules = rankedRules();
    auto json = nlohmann::json::parse(rules.toJson());
    const auto restored = yh::Ruleset::fromJson(json.dump());
    CHECK(restored && restored->toJson() == rules.toJson() && restored->proficiencyBonus(9, "advanced") == 13);
    for (const auto& bad : {nlohmann::json::object(), nlohmann::json::array({{{"id", "same"}}, {{"id", "same"}}}),
        nlohmann::json::array({{{"id", "bad"}, {"bonus", -1}}}), nlohmann::json::array({{{"id", "bad"}, {"bonus", 101}}}),
        nlohmann::json::array({{{"id", "bad"}, {"addsLevel", "yes"}}})})
    {
        auto broken = json; broken["proficiencyRanks"] = bad;
        CHECK(!yh::Ruleset::fromJson(broken.dump()));
    }
    auto broken = json; broken["proficientRank"] = "missing";
    CHECK(!yh::Ruleset::fromJson(broken.dump()));
    broken = json; broken["baseDc"] = -1;
    CHECK(!yh::Ruleset::fromJson(broken.dump()));

    yh::Character sheet;
    sheet.level = 5; sheet.dcAbility = "wis"; sheet.proficiencyRanks = {{"weapons", "advanced"}, {"str", "none"}};
    sheet.proficiencies = {"weapons", "str", "athletics"};
    auto saved = nlohmann::json::parse(sheet.toJson());
    auto loaded = yh::Character::fromJson(saved.dump());
    CHECK(loaded && loaded->toJson() == sheet.toJson() && loaded->checkProficiencyRanks(rules));
    saved.erase("proficiencyRanks"); saved.erase("dcAbility");
    loaded = yh::Character::fromJson(saved.dump());
    CHECK(loaded && loaded->proficiencyRanks.empty() && loaded->dcAbility.empty()
        && loaded->proficiencyModifier(rules, "weapons") == 7 && loaded->proficiencyModifier(rules, "stealth") == 0);
    saved["proficiencyRanks"] = nlohmann::json::array({"basic"});
    CHECK(!yh::Character::fromJson(saved.dump()));
    saved["proficiencyRanks"] = {{"weapons", ""}};
    CHECK(!yh::Character::fromJson(saved.dump()));
    saved["proficiencyRanks"] = nlohmann::json::object(); saved["level"] = 0;
    CHECK(!yh::Character::fromJson(saved.dump()));
    std::string error;
    sheet.proficiencyRanks["weapons"] = "missing";
    CHECK(!sheet.checkProficiencyRanks(rules, &error) && error.find("missing") != std::string::npos);
    sheet.proficiencyRanks["weapons"] = "basic"; sheet.proficiencyRanks["unknown"] = "basic";
    CHECK(!sheet.checkProficiencyRanks(rules, &error) && error.find("unknown") != std::string::npos);
    sheet.proficiencyRanks.erase("unknown"); sheet.dcAbility = "missing";
    CHECK(!sheet.checkProficiencyRanks(rules, &error) && error.find("DC ability") != std::string::npos);
}

void definitions()
{
    const auto rules = rankedRules();
    const auto classDef = yh::Compendium::classFromJson(R"({"id":"soldier","dcAbility":"str",
        "proficiencies":["athletics"],"proficiencyRanks":{"weapons":"advanced","armor":"basic","str":"highest","dc":"basic"}})");
    const auto creatureDef = yh::Compendium::creatureFromJson(R"({"id":"guard","level":5,"armorClass":18,
        "abilities":{"str":16,"dex":14},"dcAbility":"str","proficiencyRanks":{"weapons":"advanced","armor":"basic","dc":"master"}})");
    CHECK(classDef && creatureDef);
    if (!classDef || !creatureDef) return;
    CHECK(yh::Compendium::classToJson(*yh::Compendium::classFromJson(yh::Compendium::classToJson(*classDef)))
        == yh::Compendium::classToJson(*classDef));
    CHECK(yh::Compendium::creatureToJson(*yh::Compendium::creatureFromJson(yh::Compendium::creatureToJson(*creatureDef)))
        == yh::Compendium::creatureToJson(*creatureDef));
    CHECK(!yh::Compendium::classFromJson(R"({"id":"bad","proficiencyRanks":[]})"));
    CHECK(!yh::Compendium::creatureFromJson(R"({"id":"bad","level":0})"));
    CHECK(!yh::Compendium::creatureFromJson(R"({"id":"bad","level":1001})"));
    yh::Compendium compendium;
    compendium.classes[classDef->id] = *classDef; compendium.creatures[creatureDef->id] = *creatureDef;
    yh::Random random(7);
    const auto hero = compendium.makeCharacter(rules, "soldier", "Ana", random);
    auto creature = compendium.makeCreature(rules, "guard", "Bo", random);
    CHECK(hero && hero->proficiencyRanks == classDef->proficiencyRanks && hero->dcAbility == "str"
        && hero->proficiencyModifier(rules, "athletics") == 3);
    CHECK(creature && creature->level == 5 && creature->armorClass(rules) == 18
        && creature->attackModifier(rules) == 12 && creature->difficultyClass(rules) == 24);
    if (creature)
    {
        creature->level = 6;
        CHECK(creature->armorClass(rules) == 19 && creature->attackModifier(rules) == 13);
    }
    compendium.classes["soldier"].proficiencyRanks["weapons"] = "missing";
    compendium.creatures["guard"].dcAbility = "missing";
    CHECK(!compendium.makeCharacter(rules, "soldier", "", random) && !compendium.makeCreature(rules, "guard", "", random));
    CHECK(compendium.makeCharacter(yh::Ruleset::modern(), "soldier", "", random)
        && compendium.makeCreature(yh::Ruleset::classic(), "guard", "", random));
}

}

void proficiencies()
{
    numbers();
    serialization();
    definitions();
}

}
