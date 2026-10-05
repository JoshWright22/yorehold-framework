#include "SpellTests.h"
#include "Checks.h"

#include <yorehold/framework/assets/FileSystem.h>
#include <yorehold/framework/rpg/Compendium.h>
#include <yorehold/framework/rpg/Spell.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

namespace regression
{

namespace
{

namespace fs = std::filesystem;

// Three creatures on sheets of their own; the area is whoever the test says is in it.
struct Host : yh::EffectHost
{
    std::map<yh::EffectActor, yh::Character> sheets;
    std::vector<yh::EffectActor> area;
    yh::Character* sheet(yh::EffectActor who) override
    {
        const auto found = sheets.find(who);
        return found == sheets.end() ? nullptr : &found->second;
    }
    std::vector<yh::EffectActor> group(std::string_view which, const yh::EffectContext&) override
    {
        return which == "area" ? area : std::vector<yh::EffectActor>{};
    }
};

bool covers(const std::vector<yh::Cell>& cells, yh::Cell cell)
{
    return std::find(cells.begin(), cells.end(), cell) != cells.end();
}

void areas()
{
    std::string error;
    const auto cone = yh::ActionDefinition::fromJson(R"({"id":"fan","target":{"kind":"point","side":"any"},
        "area":{"shape":"cone","size":3},"effects":[{"do":"damage","dice":1}]})", &error);
    CHECK(cone && error.empty() && cone->target == yh::ActionDefinition::Target::Point && cone->area
        && cone->area->shape == yh::TemplateShape::Cone && cone->area->size == 3 && cone->area->directed());
    const auto burst = yh::ActionDefinition::fromJson(R"({"id":"ring","area":{"shape":"burst","size":1},"target":{"kind":"self","side":"enemy"}})");
    CHECK(burst && burst->target == yh::ActionDefinition::Target::Self && burst->area->shape == yh::TemplateShape::Circle);
    const auto line = yh::ActionDefinition::fromJson(R"({"id":"bolt","target":{"kind":"creature","range":6},"area":{"shape":"line","size":6,"width":1}})");
    CHECK(line && line->area->shape == yh::TemplateShape::Line && line->area->width == 1);
    CHECK(yh::ActionDefinition::fromJson(R"({"id":"cube","target":{"kind":"point","range":4},"area":{"shape":"square","size":3}})"));

    // Every mistake names its field.
    auto refused = [&](const char* text, const char* field) {
        return !yh::ActionDefinition::fromJson(text, &error) && error.find(field) == 0;
    };
    CHECK(refused(R"({"id":"x","target":{"kind":"point"}})", "area"));
    CHECK(refused(R"({"id":"x","area":{"shape":"cone","size":3}})", "area.shape"));
    CHECK(refused(R"({"id":"x","area":{"shape":"blob","size":3}})", "area.shape"));
    CHECK(refused(R"({"id":"x","area":{"shape":"burst"}})", "area.size"));
    CHECK(refused(R"({"id":"x","area":{"shape":"burst","size":0}})", "area.size"));
    CHECK(refused(R"({"id":"x","area":{"shape":"burst","size":2,"width":1}})", "area.width"));
    CHECK(refused(R"({"id":"x","area":{"shape":"burst","size":2,"angle":90}})", "area.angle"));
    CHECK(refused(R"({"id":"x","area":{"shape":"burst","size":2,"radius":2}})", "area.radius"));
    CHECK(refused(R"({"id":"x","target":{"kind":"self","side":"ally"}})", "target"));

    // On the map: a cone and a line start at the doer, a burst and a square sit on the aim.
    const yh::Grid grid(yh::GridType::Square, 10);
    const yh::Vec2 from = grid.center({5, 5});
    const std::vector<yh::Cell> fan = cone->area->place(grid, from, grid.center({8, 5})).cells(grid);
    CHECK(covers(fan, {6, 5}) && covers(fan, {7, 5}) && covers(fan, {8, 5})
        && !covers(fan, {9, 5}) && !covers(fan, {4, 5}) && !covers(fan, {6, 7}) && !covers(fan, {8, 7}));
    const std::vector<yh::Cell> ring = burst->area->place(grid, from, grid.center({8, 5})).cells(grid);
    CHECK(ring.size() == 5 && covers(ring, {8, 5}) && covers(ring, {8, 4}) && covers(ring, {7, 5}) && !covers(ring, {7, 4}) && !covers(ring, {5, 5}));
    const std::vector<yh::Cell> bolt = line->area->place(grid, from, grid.center({5, 2})).cells(grid);
    CHECK(covers(bolt, {5, 4}) && covers(bolt, {5, 0}) && !covers(bolt, {5, -2}) && !covers(bolt, {6, 3}) && !covers(bolt, {5, 6}));
    yh::ActionArea cube;
    cube.shape = yh::TemplateShape::Square;
    cube.size = 3;
    CHECK(cube.place(grid, from, grid.center({2, 2})).cells(grid).size() == 9);
    // Aimed at the square it stands on, a cone still points somewhere.
    CHECK(!cone->area->place(grid, from, from).cells(grid).empty());
}

void files()
{
    std::string error;
    const auto fan = yh::SpellDefinition::fromJson(R"({"id":"fan","name":"Fan","level":1,"hands":2,
        "target":{"kind":"point","side":"any"},"area":{"shape":"cone","size":3},"save":{"ability":"dex","dc":"caster"},
        "effects":[{"do":"damage","dice":"2d6","type":"fire","onSave":"half","scale":{"by":"slot","dice":"1d6"}}]})", &error);
    CHECK(fan && error.empty() && fan->level == 1 && fan->hands == 2 && !fan->concentration && fan->id() == "fan" && fan->name() == "Fan");
    CHECK(fan && fan->action.cost == 2 && !fan->action.general && fan->action.order == 501 && fan->action.effect.save.casterDc);
    const auto quick = yh::SpellDefinition::fromJson(R"({"id":"spark","cost":1,"hands":0,"concentration":true,"order":7,
        "target":{"kind":"creature","range":6},"effects":[{"do":"damage","dice":"1d4"}]})");
    CHECK(quick && quick->level == 0 && quick->hands == 0 && quick->action.cost == 1 && quick->concentration && quick->action.order == 7);
    CHECK(fan && yh::SpellDefinition::fromJson(fan->json) && yh::SpellDefinition::fromJson(fan->json)->json == fan->json);
    auto refused = [&](const char* text, const char* field) {
        return !yh::SpellDefinition::fromJson(text, &error) && error.find(field) == 0;
    };
    CHECK(refused(R"({"id":"x"})", "effects"));
    CHECK(refused(R"({"id":"x","level":-1,"effects":[{"do":"heal","dice":1}]})", "level"));
    CHECK(refused(R"({"id":"x","hands":5,"effects":[{"do":"heal","dice":1}]})", "hands"));
    CHECK(refused(R"({"id":"x","concentration":"yes","effects":[{"do":"heal","dice":1}]})", "concentration"));
    CHECK(refused(R"({"id":"x","general":true,"effects":[{"do":"heal","dice":1}]})", "general"));
    CHECK(refused(R"({"id":"x","cost":"hands","effects":[{"do":"heal","dice":1}]})", "cost"));
    CHECK(refused(R"({"id":"x","school":"fire","effects":[{"do":"heal","dice":1}]})", "school"));
    CHECK(refused(R"({"id":"x","effects":[{"do":"typo"}]})", "effects[0].do"));

    // How a ruleset casts.
    const yh::SpellRules plain;
    CHECK(plain.hands == yh::SpellRules::Hands::Free && plain.upcast && plain.onDamage == yh::SpellRules::Damage::Save
        && plain.concentrationDc(4) == 10 && plain.concentrationDc(31) == 15);
    const auto loose = yh::SpellRules::fromJson(R"({"hands":"ignored","slotPrefix":"mana-","upcast":false,
        "concentration":{"onDamage":"breaks","ability":"wis","minimumDc":12,"damageShare":1,"endsWhenDown":false}})", &error);
    CHECK(loose && loose->hands == yh::SpellRules::Hands::Ignored && loose->slotPrefix == "mana-" && !loose->upcast
        && loose->onDamage == yh::SpellRules::Damage::Breaks && loose->saveAbility == "wis" && loose->concentrationDc(20) == 20
        && !loose->endsWhenDown);
    CHECK(loose && yh::SpellRules::fromJson(loose->toJson()) && yh::SpellRules::fromJson(loose->toJson())->toJson() == loose->toJson());
    CHECK(yh::SpellRules::fromJson("{}") && yh::SpellRules::fromJson("{}")->toJson() == plain.toJson());
    CHECK(!yh::SpellRules::fromJson(R"({"hands":"three"})", &error) && error.find("hands") == 0);
    CHECK(!yh::SpellRules::fromJson(R"({"slots":1})", &error) && error.find("slots") == 0);
    CHECK(!yh::SpellRules::fromJson(R"({"concentration":{"onDamage":"maybe"}})", &error) && error.find("concentration.onDamage") == 0);
    CHECK(!yh::SpellRules::fromJson(R"({"concentration":{"dc":3}})", &error) && error.find("concentration.dc") == 0);
    const auto rules = yh::Ruleset::modern();
    auto odd = plain;
    odd.saveAbility = "luck";
    CHECK(plain.check(rules) && !odd.check(rules, &error) && error.find("luck") != std::string::npos);
    odd.onDamage = yh::SpellRules::Damage::Breaks;
    CHECK(odd.check(rules));
}

void slotsAndHands()
{
    const auto cantrip = *yh::SpellDefinition::fromJson(R"({"id":"spark","effects":[{"do":"damage","dice":1}]})");
    const auto first = *yh::SpellDefinition::fromJson(R"({"id":"fan","level":1,"hands":2,"effects":[{"do":"damage","dice":1}]})");
    yh::SpellRules rules;
    yh::Character caster;
    caster.resources["slots-1"] = {1, 2};
    caster.resources["slots-2"] = {1, 1};

    // Cantrips spend nothing; a spell takes the lowest slot that is left.
    CHECK(yh::slotFor(caster, cantrip, rules) == 0 && yh::spendSlot(caster, rules, 0) && caster.resources["slots-1"].current == 1);
    CHECK(yh::slotFor(caster, first, rules) == 1 && yh::canCast(caster, first, rules));
    CHECK(yh::spendSlot(caster, rules, 1) && caster.resources["slots-1"].current == 0 && !yh::spendSlot(caster, rules, 1));
    CHECK(yh::slotFor(caster, first, rules) == 2 && yh::slotFor(caster, first, rules, 2) == 2 && !yh::slotFor(caster, first, rules, 1)
        && !yh::slotFor(caster, first, rules, 3));
    rules.upcast = false;
    std::string why;
    CHECK(!yh::slotFor(caster, first, rules) && !yh::slotFor(caster, first, rules, 2) && !yh::canCast(caster, first, rules, &why)
        && why == "no spell slot left" && yh::canCast(caster, cantrip, rules));
    rules.upcast = true;
    CHECK(yh::spendSlot(caster, rules, 2) && !yh::slotFor(caster, first, rules));
    rules.slotPrefix = "mana-";
    caster.resources["mana-1"] = {1, 1};
    CHECK(yh::slotFor(caster, first, rules) == 1);

    // Hands: what is held is in the way, where the rules say so.
    yh::Item staff;
    staff.id = "staff";
    staff.slot = "mainHand";
    caster.inventory.push_back(staff);
    CHECK(caster.freeHands() == 2 && caster.equip(0) && caster.freeHands() == 1);
    CHECK(yh::canCast(caster, cantrip, rules) && !yh::canCast(caster, first, rules, &why) && why == "needs 2 free hands");
    rules.hands = yh::SpellRules::Hands::Ignored;
    CHECK(yh::canCast(caster, first, rules));
    rules.hands = yh::SpellRules::Hands::Free;
    caster.unequip(0);
    CHECK(yh::canCast(caster, first, rules));

    // A rest refills what its definition names.
    caster.resources["slots-1"].current = 0;
    caster.resources["focus"] = {0, 2};
    CHECK(caster.restoreResources({}) == 0 && caster.restoreResources({"slots-*"}) == 3 && caster.resources["slots-1"].current == 2
        && caster.resources["slots-2"].current == 1 && caster.resources["focus"].current == 0);
    CHECK(caster.restoreResources({"focus"}) == 2 && caster.restoreResources({"*"}) == 0);
    caster.resources["mana-1"].current = 0;
    CHECK(caster.restoreResources({"*"}) == 1);
    auto withRest = yh::Ruleset::modern();
    withRest.rests = {{"night", "Night", {}, 0, {"slots-*", "focus"}}};
    const auto again = yh::Ruleset::fromJson(withRest.toJson());
    CHECK(again && again->rests.size() == 1 && again->rests[0].restores == std::vector<std::string>{"slots-*", "focus"});
    CHECK(yh::Ruleset::fromJson(yh::Ruleset::modern().toJson())->rests[0].restores.empty());
}

void concentrating()
{
    auto rules = yh::Ruleset::modern();
    yh::ConditionDefinition mired;
    mired.id = "mired";
    rules.conditions.push_back(mired);
    Host host;
    for (const yh::EffectActor who : {0, 1, 2})
    {
        host.sheets[who].stats.setBase("maxHp", 50);
        host.sheets[who].hp = 50;
        for (const auto& ability : rules.abilities)
            host.sheets[who].stats.setBase(ability.id, 10);
    }
    host.area = {1, 2};
    yh::Random random(11);
    yh::EffectContext context;
    context.rules = &rules;
    context.random = &random;
    context.self = 0;
    context.targets = host.area;
    context.source = "mire";
    const auto effect = yh::Effect::fromJson(R"([{"do":"condition","id":"mired","duration":3,"target":"area"},
        {"do":"modifier","stat":"ac","value":-1,"duration":3},{"do":"damage","dice":2}])");
    CHECK(effect && effect->check(rules));
    const yh::EffectResult result = effect->run(host, context);
    yh::Concentration held = yh::Concentration::begin("mire", result);
    CHECK(held.active() && held.spell == "mire" && held.holds.size() == 4 && host.sheets[1].hasCondition("mired")
        && host.sheets[2].hasCondition("effect:mire") && host.sheets[1].hp == 48);

    // It survives a save file.
    const auto back = yh::Concentration::fromJson(held.toJson());
    CHECK(back && back->spell == "mire" && back->holds == held.holds);
    CHECK(yh::Concentration::fromJson(yh::Concentration{}.toJson()) && !yh::Concentration::fromJson(yh::Concentration{}.toJson())->active());
    CHECK(!yh::Concentration::fromJson(R"({"holds":[]})") && !yh::Concentration::fromJson(R"({"spell":"x","holds":[{"who":"a"}]})"));

    // What has run out or been taken off is forgotten; the rest comes off when it ends.
    const yh::Concentration::Sheets sheets = [&](yh::EffectActor who) { return host.sheet(who); };
    host.sheets[1].removeCondition("mired");
    CHECK(held.tidy(sheets) && held.holds.size() == 3);
    const auto removed = held.end(sheets);
    CHECK(removed.size() == 3 && !held.active() && held.holds.empty() && !host.sheets[2].hasCondition("mired")
        && !host.sheets[1].hasCondition("effect:mire") && !host.sheets[2].hasCondition("effect:mire"));
    CHECK(host.sheets[1].armorClass(rules) == host.sheets[0].armorClass(rules));
    yh::Concentration brief = yh::Concentration::begin("mire", effect->run(host, context));
    for (const yh::EffectActor who : {1, 2})
        for (int round = 0; round < 4; round++)
            host.sheets[who].endRound(rules);
    CHECK(!brief.tidy(sheets) && !brief.active());
    // A spell that leaves nothing behind has nothing to concentrate on.
    yh::Concentration nothing = yh::Concentration::begin("zap", {});
    CHECK(nothing.active() && !nothing.tidy(sheets));

    // Damage: a save against the larger of the floor and a share of the damage.
    yh::SpellRules spells;
    yh::Character& caster = host.sheets[0];
    CHECK(!yh::concentrationCheck(caster, rules, spells, 0, random).rolled && yh::concentrationCheck(caster, rules, spells, 0, random).kept);
    caster.stats.setBase("con", 1000);
    const yh::ConcentrationCheck sure = yh::concentrationCheck(caster, rules, spells, 60, random);
    CHECK(sure.rolled && sure.kept && sure.dc == 30 && sure.roll.total >= 30);
    spells.minimumDc = 600;
    const yh::ConcentrationCheck lost = yh::concentrationCheck(caster, rules, spells, 1, random);
    CHECK(lost.rolled && !lost.kept && lost.dc == 600);
    spells.onDamage = yh::SpellRules::Damage::Breaks;
    CHECK(!yh::concentrationCheck(caster, rules, spells, 1, random).rolled && !yh::concentrationCheck(caster, rules, spells, 1, random).kept);
    spells.onDamage = yh::SpellRules::Damage::Ignored;
    CHECK(yh::concentrationCheck(caster, rules, spells, 100, random).kept);
}

void lists()
{
    std::string error;
    const auto mage = yh::Compendium::classFromJson(R"({"id":"mage","name":"Mage","hitDie":6,
        "spells":{"0":["spark"],"1":["fan","mire"],"2":["gale"]},"levels":[{"slots":{"1":2}},{"slots":{"1":3}},{"slots":{"1":4,"2":2}}]})", &error);
    CHECK(mage && error.empty() && mage->spells.at(1) == std::vector<std::string>{"fan", "mire"});
    if (!mage) return;
    const auto again = yh::Compendium::classFromJson(yh::Compendium::classToJson(*mage));
    CHECK(again && again->spells == mage->spells && yh::Compendium::classToJson(*again) == yh::Compendium::classToJson(*mage));
    CHECK(!yh::Compendium::classFromJson(R"({"id":"x","spells":["spark"]})", &error) && error.find("spells") == 0);
    CHECK(!yh::Compendium::classFromJson(R"({"id":"x","spells":{"one":["spark"]}})", &error) && error.find("spells.one") == 0);
    CHECK(!yh::Compendium::classFromJson(R"({"id":"x","spells":{"1":["Big Spark"]}})", &error) && error.find("spells.1") == 0);

    const fs::path root = fs::temp_directory_path() / "yorehold-spells-test";
    fs::remove_all(root);
    for (const char* folder : {"good/spells", "missing/spells", "level/spells", "broken/spells", "named/spells"})
        fs::create_directories(root / folder);
    const char* step = R"("effects":[{"do":"damage","dice":"1d4"}]})";
    for (const char* folder : {"good", "missing", "level"})
    {
        std::ofstream(root / folder / "spells/spark.json") << R"({"id":"spark",)" << step;
        std::ofstream(root / folder / "spells/fan.json") << R"({"id":"fan","level":1,"hands":2,)" << step;
        std::ofstream(root / folder / "spells/gale.json") << R"({"id":"gale","level":)" << (std::string(folder) == "level" ? 3 : 2) << "," << step;
    }
    std::ofstream(root / "good/spells/mire.json") << R"({"id":"mire","level":1,"concentration":true,)" << step;
    std::ofstream(root / "level/spells/mire.json") << R"({"id":"mire","level":1,)" << step;
    std::ofstream(root / "broken/spells/spark.json") << R"({"id":"spark","effects":[{"do":"typo"}]})";
    std::ofstream(root / "named/spells/spark.json") << R"({"id":"sparkle",)" << step;
    yh::FileSystem disk;
    CHECK(disk.mountFolder(root.string(), "test"));
    yh::Compendium compendium;
    compendium.classes["mage"] = *mage;
    yh::ClassDefinition soldier;
    soldier.id = "soldier";
    soldier.name = "Soldier";
    compendium.classes["soldier"] = soldier;
    CHECK(!compendium.loadOptions(disk, "missing", &error) && error.find("classes/mage.json") == 0 && error.find("no spell \"mire\"") != std::string::npos);
    CHECK(!compendium.loadOptions(disk, "level", &error) && error.find("spells.2") != std::string::npos && error.find("level 3") != std::string::npos);
    CHECK(!compendium.loadOptions(disk, "broken", &error) && error.find("spark.json") != std::string::npos && error.find("effects[0].do") != std::string::npos);
    CHECK(!compendium.loadOptions(disk, "named", &error) && error.find("doesn't match the file name") != std::string::npos && compendium.spells.empty());
    CHECK(compendium.loadOptions(disk, "good", &error) && error.empty() && compendium.spells.size() == 4
        && compendium.spell("mire") && compendium.spell("mire")->concentration && !compendium.spell("nothing"));
    disk.unmount("test");
    fs::remove_all(root);

    // A sheet gets its classes' cantrips and what they list at the levels it has slots for.
    const auto rules = yh::Ruleset::modern();
    yh::CharacterChoices choices;
    choices.name = "Mira";
    for (const auto& ability : rules.abilities)
        choices.scores[ability.id] = 10;
    choices.levels = {{"mage", {}}};
    auto sheet = compendium.build(rules, choices, &error);
    CHECK(sheet && error.empty() && sheet->spells == std::vector<std::string>{"spark", "fan", "mire"});
    choices.levels = {{"mage", {}}, {"mage", {}}, {"mage", {}}};
    const auto third = compendium.build(rules, choices, &error);
    CHECK(third && third->spells == std::vector<std::string>{"spark", "fan", "mire", "gale"});
    choices.levels = {{"soldier", {}}};
    CHECK(compendium.build(rules, choices) && compendium.build(rules, choices)->spells.empty());
    choices.levels = {{"soldier", {}}, {"mage", {}}};
    CHECK(compendium.build(rules, choices) && compendium.build(rules, choices)->spells.size() == 3);

    // Saved sheets keep their spells, and a rebuilt one takes the new list.
    if (!sheet || !third) return;
    const auto saved = yh::Character::fromJson(sheet->toJson());
    CHECK(saved && saved->spells == sheet->spells);
    CHECK(yh::Character::fromJson(yh::Character{}.toJson()) && yh::Character::fromJson(yh::Character{}.toJson())->spells.empty());
    sheet->adoptBuild(*third);
    CHECK(sheet->spells.size() == 4 && sheet->resources.at("slots-2").max == 2);
}

// Spells that spend a resource instead of a slot, and classes that prepare or keep a few.
void castingStyles()
{
    std::string error;
    const char* step = R"("effects":[{"do":"damage","dice":"1d4"}]})";
    const auto dart = yh::SpellDefinition::fromJson(std::string(R"({"id":"dart","level":1,"spends":{"focus":1},)") + step, &error);
    CHECK(dart && error.empty() && dart->spends == std::map<std::string, int>{{"focus", 1}});
    const auto older = yh::SpellDefinition::fromJson(std::string(R"({"id":"dart","level":1,"spends":["focus","focus"],)") + step);
    CHECK(older && older->spends == std::map<std::string, int>{{"focus", 2}});
    CHECK(!yh::SpellDefinition::fromJson(std::string(R"({"id":"x","spends":{"focus":0},)") + step, &error) && error.find("spends") == 0);
    CHECK(!yh::SpellDefinition::fromJson(std::string(R"({"id":"x","spends":"focus",)") + step, &error) && error.find("spends") == 0);
    if (!dart) return;

    // A focus spell needs no slot and is refused with an empty pool.
    const yh::SpellRules rules;
    yh::Character caster;
    caster.resources["focus"] = {1, 1};
    std::string why;
    CHECK(yh::slotFor(caster, *dart, rules) == 0 && yh::canCast(caster, *dart, rules));
    CHECK(yh::spendCasting(caster, *dart, rules, 0) && caster.resources["focus"].current == 0);
    CHECK(!yh::canCast(caster, *dart, rules, &why) && why == "needs 1 focus" && !yh::spendCasting(caster, *dart, rules, 0));
    caster.resources["slots-1"] = {1, 1};
    const auto fan = yh::SpellDefinition::fromJson(std::string(R"({"id":"fan","level":1,"hands":0,)") + step);
    CHECK(fan && yh::spendCasting(caster, *fan, rules, 1) && caster.resources["slots-1"].current == 0 && !yh::spendCasting(caster, *fan, rules, 1));

    // Classes: a level row's "spells" is how many it prepares or keeps; a row without one keeps the last.
    yh::Compendium compendium;
    for (const char* text : {R"({"id":"spark",)", R"({"id":"fan","level":1,)", R"({"id":"mire","level":1,)", R"({"id":"ward","level":1,)",
             R"({"id":"gale","level":2,)", R"({"id":"dart","level":1,"spends":{"focus":1},)"})
    {
        const auto spell = yh::SpellDefinition::fromJson(std::string(text) + step);
        if (spell) compendium.spells[spell->id()] = *spell;
    }
    const char* levels = R"("levels":[{"slots":{"1":2},"spells":2,"features":[{"id":"pool","resources":{"focus":1}}]},{"slots":{"1":3}},{"slots":{"1":4,"2":2},"spells":3}]})";
    const auto sage = yh::Compendium::classFromJson(std::string(R"({"id":"sage","casting":"prepared","spells":{"0":["spark"],"1":["fan","mire","ward","dart"],"2":["gale"]},)") + levels, &error);
    const auto bard = yh::Compendium::classFromJson(std::string(R"({"id":"bard","casting":"spontaneous","spells":{"1":["fan","mire","ward"]},)") + levels);
    CHECK(sage && error.empty() && sage->casting == "prepared" && sage->levels[0].spells == 2 && sage->levels[1].spells == 0);
    CHECK(sage && yh::Compendium::classFromJson(yh::Compendium::classToJson(*sage))
        && yh::Compendium::classToJson(*yh::Compendium::classFromJson(yh::Compendium::classToJson(*sage))) == yh::Compendium::classToJson(*sage));
    CHECK(!yh::Compendium::classFromJson(R"({"id":"x","casting":"sometimes"})", &error) && error.find("casting") == 0);
    CHECK(!yh::Compendium::classFromJson(R"({"id":"x","levels":[{"spells":-1}]})", &error) && error.find("levels[0].spells") == 0);
    if (!sage || !bard) return;
    compendium.classes["sage"] = *sage;
    compendium.classes["bard"] = *bard;

    const auto ruleset = yh::Ruleset::modern();
    yh::CharacterChoices choices;
    choices.name = "Ola";
    for (const auto& ability : ruleset.abilities)
        choices.scores[ability.id] = 10;
    choices.levels = {{"sage", {}}};
    auto first = compendium.build(ruleset, choices, &error);
    CHECK(first && error.empty() && first->spells == std::vector<std::string>{"spark", "dart", "fan", "mire"}
        && first->preparable == std::vector<std::string>{"fan", "mire", "ward"} && first->prepareLimit == 2
        && first->prepared == std::vector<std::string>{"fan", "mire"});
    if (!first) return;
    CHECK(!first->prepare({"fan", "mire", "ward"}, &why) && why.find("at most 2") != std::string::npos);
    CHECK(!first->prepare({"gale"}) && !first->prepare({}) && !first->prepare({"ward", "ward"}));
    CHECK(first->prepare({"ward"}) && first->spells == std::vector<std::string>{"spark", "dart", "ward"} && first->prepared == std::vector<std::string>{"ward"});
    const auto saved = yh::Character::fromJson(first->toJson());
    CHECK(saved && saved->prepared == first->prepared && saved->preparable == first->preparable && saved->prepareLimit == 2);

    // Levelling up keeps the choice, widens the list and raises the count.
    choices.levels = {{"sage", {}}, {"sage", {}}, {"sage", {}}};
    const auto third = compendium.build(ruleset, choices, &error);
    CHECK(third && third->prepareLimit == 3 && third->preparable.size() == 4 && third->prepared.size() == 3);
    if (!third) return;
    first->adoptBuild(*third);
    CHECK(first->prepared == std::vector<std::string>{"ward"} && first->prepareLimit == 3
        && first->spells == std::vector<std::string>{"spark", "dart", "ward"} && first->prepare({"ward", "gale", "fan"}));

    // A spontaneous caster keeps its picks, then the list's first.
    choices.levels = {{"bard", {{"spells", {"ward"}}}}};
    const auto bardSheet = compendium.build(ruleset, choices, &error);
    CHECK(bardSheet && error.empty() && bardSheet->spells == std::vector<std::string>{"ward", "fan"} && bardSheet->preparable.empty());
}

}

void spells()
{
    areas();
    files();
    slotsAndHands();
    concentrating();
    lists();
    castingStyles();
}

}
