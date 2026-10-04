#include "Checks.h"
#include "EffectTests.h"

#include <yorehold/framework/assets/FileSystem.h>
#include <yorehold/framework/rpg/Action.h>
#include <yorehold/framework/rpg/Combat.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace regression
{
namespace
{

using Kind = yh::EffectEvent::Kind;

const char* authored = R"({"id":"t","name":"Test","abilities":[{"id":"str","name":"Str"},{"id":"dex","name":"Dex"},{"id":"con","name":"Con"},{"id":"wis","name":"Wis"}],
    "skills":[{"id":"athletics","name":"Athletics","ability":"str"}],
    "conditions":[
        {"id":"burning","name":"Burning","duration":3},
        {"id":"shaken","name":"Shaken","stacking":"value","maxValue":5},
        {"id":"asleep","name":"Asleep","ends":["damage"]},
        {"id":"downed","name":"Downed","ends":["healed"]},
        {"id":"unseen","name":"Unseen","ends":["attack"]}
    ]})";

// Four creatures: 0 and 1 on one side, 2 and 3 on the other. Everything a step asks of the map is written down.
struct TestHost : yh::EffectHost
{
    std::vector<yh::Character> sheets;
    std::vector<yh::EffectActor> area;
    std::vector<std::string> calls;
    std::string damageType;
    size_t pick = 0;

    yh::Character* sheet(yh::EffectActor who) override
    {
        return who >= 0 && who < static_cast<int>(sheets.size()) ? &sheets[static_cast<size_t>(who)] : nullptr;
    }
    std::vector<yh::EffectActor> group(std::string_view which, const yh::EffectContext& context) override
    {
        if (which == "area")
            return area;
        std::vector<yh::EffectActor> out;
        for (int i = 0; i < static_cast<int>(sheets.size()); i++)
            if ((i / 2 == context.self / 2) == (which == "allies"))
                out.push_back(i);
        return out;
    }
    int damage(yh::EffectActor who, int amount, std::string_view type, const yh::EffectContext& context) override
    {
        damageType = type;
        return yh::EffectHost::damage(who, amount, type, context);
    }
    bool move(yh::EffectActor who, std::string_view how, int squares, const yh::EffectContext&) override
    {
        calls.push_back("move " + std::to_string(who) + " " + std::string(how) + " " + std::to_string(squares));
        return true;
    }
    bool summon(std::string_view creature, int count, int rounds, const yh::EffectContext&) override
    {
        calls.push_back("summon " + std::string(creature) + " " + std::to_string(count) + " " + std::to_string(rounds));
        return true;
    }
    bool light(yh::EffectActor who, float radius, int rounds, const yh::EffectContext&) override
    {
        calls.push_back("light " + std::to_string(who) + " " + std::to_string(static_cast<int>(radius)) + " " + std::to_string(rounds));
        return true;
    }
    bool surface(std::string_view id, float size, int rounds, const yh::EffectContext&) override
    {
        calls.push_back("surface " + std::string(id) + " " + std::to_string(static_cast<int>(size)) + " " + std::to_string(rounds));
        return true;
    }
    bool flag(std::string_view name, bool set, const yh::EffectContext&) override
    {
        calls.push_back(std::string(set ? "set " : "clear ") + std::string(name));
        return true;
    }
    size_t choose(const std::vector<std::string>&, const yh::EffectContext&) override { return pick; }
};

// A host that supports nothing beyond sheets.
struct BareHost : yh::EffectHost
{
    yh::Character one;
    yh::Character* sheet(yh::EffectActor who) override { return who == 0 ? &one : nullptr; }
    std::vector<yh::EffectActor> group(std::string_view, const yh::EffectContext&) override { return {}; }
};

yh::Character plain(const char* name)
{
    yh::Character c;
    c.name = name;
    for (const char* ability : {"str", "dex", "con", "wis"})
        c.stats.setBase(ability, 10);
    c.stats.setBase("ac", 12);
    c.stats.setBase("speed", 30);
    c.stats.setBase("maxHp", 100);
    c.hp = 100;
    return c;
}

TestHost table()
{
    TestHost host;
    for (const char* name : {"Ana", "Bo", "Gik", "Mog"})
        host.sheets.push_back(plain(name));
    return host;
}

yh::Effect effect(const char* json)
{
    std::string error;
    const std::optional<yh::Effect> parsed = yh::Effect::fromJson(json, &error);
    CHECK(parsed && error.empty());
    if (!parsed)
        std::fprintf(stderr, "  %s\n", error.c_str());
    return parsed.value_or(yh::Effect{});
}

std::string problem(const char* json)
{
    std::string error;
    CHECK(!yh::Effect::fromJson(json, &error));
    return error;
}

bool mentions(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

int count(const yh::EffectResult& result, Kind kind)
{
    return static_cast<int>(std::count_if(result.events.begin(), result.events.end(), [&](const yh::EffectEvent& e) { return e.kind == kind; }));
}

const yh::EffectEvent* first(const yh::EffectResult& result, Kind kind)
{
    const auto found = std::find_if(result.events.begin(), result.events.end(), [&](const yh::EffectEvent& e) { return e.kind == kind; });
    return found == result.events.end() ? nullptr : &*found;
}

void files(const yh::Ruleset& rules)
{
    // The two shapes: a list of steps, or an object with the save beside them.
    const yh::Effect spell = effect(R"({"save":{"ability":"dex","dc":"caster"},"effects":[{"do":"damage","dice":"3d6","type":"fire","onSave":"half","target":"area"}]})");
    CHECK(spell.save.ability == "dex" && spell.save.casterDc && spell.steps.size() == 1 && spell.steps[0].kind == yh::EffectStep::Kind::Damage
        && spell.steps[0].amount == "3d6" && spell.steps[0].type == "fire" && spell.steps[0].onSave == yh::EffectStep::OnSave::Half
        && spell.steps[0].target == "area");
    const yh::Effect list = effect(R"([{"do":"heal","dice":5,"target":"self"},{"do":"roll","kind":"attack","steps":[{"do":"damage","dice":"weapon","when":"hit"}]}])");
    CHECK(list.save.ability.empty() && list.steps.size() == 2 && list.steps[0].amount == "5" && list.steps[1].steps.size() == 1
        && list.steps[1].steps[0].when == "hit" && list.steps[1].steps[0].amount == "weapon");
    CHECK(effect("[]").empty() && effect(R"({"effects":[]})").empty());
    CHECK(spell.check(rules) && list.check(rules));

    // An unknown step, and anything misspelt or out of range, names the field it is in.
    CHECK(mentions(problem(R"([{"do":"heal","dice":1},{"do":"explode"}])"), "effects[1].do") && mentions(problem(R"([{"do":"explode"}])"), "explode"));
    CHECK(mentions(problem(R"([{"do":"damage","dise":"1d6"}])"), "effects[0].dice"));
    CHECK(mentions(problem(R"([{"do":"damage","dice":"1d6","colour":"red"}])"), "effects[0].colour"));
    CHECK(mentions(problem(R"([{"do":"damage","dice":"lots"}])"), "effects[0].dice"));
    CHECK(mentions(problem(R"([{"do":"damage","dice":"1d6","when":"teatime"}])"), "effects[0].when"));
    CHECK(mentions(problem(R"([{"do":"damage","dice":"1d6","target":"everyone"}])"), "effects[0].target"));
    CHECK(mentions(problem(R"([{"do":"heal","dice":"1d6","onSave":"half"}])"), "effects[0].onSave"));
    CHECK(mentions(problem(R"([{"do":"roll","kind":"attack","steps":[{"do":"heal","dice":1},{"do":"damage","dice":"x"}]}])"), "effects[0].steps[1].dice"));
    CHECK(mentions(problem(R"([{"do":"roll","kind":"attack"}])"), "effects[0].steps"));
    CHECK(mentions(problem(R"([{"do":"roll","kind":"guess","steps":[]}])"), "effects[0].kind"));
    CHECK(mentions(problem(R"([{"do":"roll","kind":"save","steps":[]}])"), "effects[0].ability"));
    CHECK(mentions(problem(R"([{"do":"condition","id":"burning","duration":0}])"), "effects[0].duration"));
    CHECK(mentions(problem(R"([{"do":"modifier","stat":"ac","value":1,"op":"square"}])"), "effects[0].op"));
    CHECK(mentions(problem(R"([{"do":"move","how":"fling"}])"), "effects[0].how"));
    CHECK(mentions(problem(R"([{"do":"damage","dice":"1d6","scale":{"by":"age","dice":"1d6"}}])"), "effects[0].scale.by"));
    CHECK(mentions(problem(R"([{"do":"flag","id":"x","scale":{"by":"level","value":1}}])"), "effects[0].scale"));
    CHECK(mentions(problem(R"([{"do":"choose","options":[{"name":"a","steps":[]},{"steps":[]}]}])"), "effects[0].options[1].name"));
    CHECK(mentions(problem(R"({"effects":[],"save":{"dc":12}})"), "save.ability"));
    CHECK(mentions(problem(R"({"effects":[],"sav":{}})"), "sav"));
    CHECK(!problem("{not json").empty() && !problem(R"("damage")").empty());
    std::string deep = R"({"do":"heal","dice":1})";
    for (int i = 0; i < 8; i++)
        deep = R"({"do":"repeat","times":1,"steps":[)" + deep + "]}";
    CHECK(mentions(problem(("[" + deep + "]").c_str()), "too deep"));

    // What needs the ruleset: its conditions, abilities and skills, and a save for onSave to answer to.
    std::string error;
    CHECK(!effect(R"([{"do":"condition","id":"cursed"}])").check(rules, &error) && mentions(error, "effects[0].id") && mentions(error, "cursed"));
    CHECK(!effect(R"({"save":{"ability":"luck"},"effects":[]})").check(rules, &error) && mentions(error, "save.ability"));
    CHECK(!effect(R"([{"do":"roll","kind":"check","ability":"juggling","steps":[]}])").check(rules, &error) && mentions(error, "effects[0].ability"));
    CHECK(!effect(R"([{"do":"roll","kind":"check","ability":"athletics","against":"juggling","steps":[]}])").check(rules, &error) && mentions(error, "effects[0].against"));
    CHECK(!effect(R"([{"do":"damage","dice":"1d6","onSave":"half"}])").check(rules, &error) && mentions(error, "effects[0].onSave"));
    CHECK(effect(R"([{"do":"roll","kind":"save","ability":"dex","steps":[{"do":"damage","dice":"1d6","onSave":"half"}]}])").check(rules, &error));
    CHECK(effect(R"([{"do":"roll","kind":"check","ability":"athletics","against":"str","steps":[{"do":"condition","id":"shaken","when":"success"}]}])").check(rules, &error));
}

void stepKinds(const yh::Ruleset& rules)
{
    yh::Random random(7);
    yh::EffectContext context;
    context.rules = &rules;
    context.random = &random;
    context.self = 0;
    context.targets = {2};
    context.source = "test";

    // Damage: off the target, in the type named, ending what being hit ends.
    TestHost host = table();
    host.sheets[2].addCondition(rules, "asleep");
    yh::EffectResult result = effect(R"([{"do":"damage","dice":10,"type":"fire"}])").run(host, context);
    CHECK(host.sheets[2].hp == 90 && host.sheets[0].hp == 100 && host.damageType == "fire" && !host.sheets[2].hasCondition("asleep"));
    CHECK(result.events.size() == 2 && result.events[0].kind == Kind::Damage && result.events[0].who == 2 && result.events[0].by == 0
        && result.events[0].amount == 10 && result.events[0].id == "fire" && !result.events[0].dropped
        && result.events[1].kind == Kind::ConditionEnded && result.events[1].id == "asleep");
    host.sheets[2].tempHp = 4;
    host.sheets[2].hp = 5;
    result = effect(R"([{"do":"damage","dice":"9"}])").run(host, context);
    CHECK(host.sheets[2].hp == 0 && host.sheets[2].tempHp == 0 && result.events[0].dropped && result.events[0].id == "untyped");
    host = table();
    effect(R"([{"do":"damage","dice":"1-5","minimum":1}])").run(host, context);
    CHECK(host.sheets[2].hp == 99); // a blow that lands always does something where the step says so
    effect(R"([{"do":"damage","dice":"1-5"}])").run(host, context);
    CHECK(host.sheets[2].hp == 99);

    // Heal: up to the maximum, and getting someone up ends what being healed ends.
    host.sheets[2].hp = 0;
    host.sheets[2].addCondition(rules, "downed");
    result = effect(R"([{"do":"heal","dice":30}])").run(host, context);
    CHECK(host.sheets[2].hp == 30 && !host.sheets[2].hasCondition("downed") && result.events.size() == 2 && result.events[0].kind == Kind::Heal
        && result.events[0].amount == 30 && result.events[1].kind == Kind::ConditionEnded);
    host.sheets[2].hp = 95;
    result = effect(R"([{"do":"heal","dice":30}])").run(host, context);
    CHECK(host.sheets[2].hp == 100 && result.events[0].amount == 5);

    // Temporary HP: the larger stays, they never add up.
    effect(R"([{"do":"tempHp","dice":8,"target":"self"}])").run(host, context);
    effect(R"([{"do":"tempHp","dice":5,"target":"self"}])").run(host, context);
    CHECK(host.sheets[0].tempHp == 8 && host.sheets[2].tempHp == 0);

    // Conditions go on with the step's duration and value, or the definition's, and come off again.
    result = effect(R"([{"do":"condition","id":"burning"},{"do":"condition","id":"shaken","value":2,"duration":4},{"do":"condition","id":"shaken"}])").run(host, context);
    CHECK(host.sheets[2].hasCondition("burning") && host.sheets[2].conditions[0].roundsLeft == 3 && host.sheets[2].conditionValue("shaken") == 3);
    CHECK(count(result, Kind::ConditionAdded) == 3 && result.events[1].amount == 2 && result.events[2].amount == 3);
    result = effect(R"([{"do":"condition","id":"burning","remove":true},{"do":"condition","id":"asleep","remove":true}])").run(host, context);
    CHECK(!host.sheets[2].hasCondition("burning") && count(result, Kind::ConditionRemoved) == 1 && result.events[0].id == "burning");

    // A modifier lasts its rounds, and doing the same thing again does not double it.
    const yh::Effect warded = effect(R"([{"do":"modifier","stat":"ac","value":2,"duration":2,"target":"self"},{"do":"modifier","stat":"speed","op":"multiply","value":0.5,"duration":2,"target":"self"}])");
    const int ac = host.sheets[0].armorClass(rules);
    result = warded.run(host, context);
    CHECK(host.sheets[0].armorClass(rules) == ac + 2 && host.sheets[0].speedFeet() == 15 && count(result, Kind::Modifier) == 2);
    warded.run(host, context);
    CHECK(host.sheets[0].armorClass(rules) == ac + 2 && host.sheets[0].speedFeet() == 15 && host.sheets[0].hasCondition("effect:test"));
    const std::optional<yh::Character> reloaded = yh::Character::fromJson(host.sheets[0].toJson());
    CHECK(reloaded && reloaded->armorClass(rules) == ac + 2 && reloaded->hasCondition("effect:test")); // it travels with a save
    host.sheets[0].endRound(rules);
    CHECK(host.sheets[0].armorClass(rules) == ac + 2);
    host.sheets[0].endRound(rules);
    CHECK(host.sheets[0].armorClass(rules) == ac && host.sheets[0].speedFeet() == 30 && host.sheets[0].conditions.empty());

    // Resources are spent and restored within what the sheet has.
    host.sheets[0].resources["rage"] = {2, 3};
    result = effect(R"([{"do":"resource","id":"rage","amount":1,"target":"self"}])").run(host, context);
    CHECK(host.sheets[0].resources["rage"].current == 1 && result.events.size() == 1 && result.events[0].kind == Kind::Resource && result.events[0].amount == -1);
    effect(R"([{"do":"resource","id":"rage","op":"restore","amount":9,"target":"self"}])").run(host, context);
    CHECK(host.sheets[0].resources["rage"].current == 3);
    effect(R"([{"do":"resource","id":"rage","op":"spend","amount":9,"target":"self"}])").run(host, context);
    CHECK(host.sheets[0].resources["rage"].current == 0);
    CHECK(effect(R"([{"do":"resource","id":"ki","target":"self"}])").run(host, context).events.empty()); // it has none

    // Everything on the map goes to the host, with the numbers from the file.
    result = effect(R"([{"do":"move","how":"push","distance":2},{"do":"move","how":"teleport","distance":"speed","target":"self"},
        {"do":"summon","id":"wolf","count":2,"duration":10},{"do":"light","radius":4,"duration":5,"target":"self"},
        {"do":"surface","id":"grease","size":2,"duration":3},{"do":"flag","id":"bell_rung"},{"do":"flag","id":"door_shut","remove":true}])").run(host, context);
    CHECK(host.calls == std::vector<std::string>{"move 2 push 2", "move 0 teleport 6", "summon wolf 2 10", "light 0 4 5", "surface grease 2 3", "set bell_rung", "clear door_shut"});
    CHECK(result.events.size() == 7 && result.events[0].kind == Kind::Move && result.events[2].kind == Kind::Summon && result.events[2].amount == 2
        && result.events[3].kind == Kind::Light && result.events[4].kind == Kind::Surface && result.events[5].kind == Kind::Flag
        && result.events[5].amount == 1 && result.events[6].amount == 0);
    // A host that supports none of that: the steps do nothing and report nothing.
    BareHost bare;
    bare.one = plain("Solo");
    result = effect(R"([{"do":"move","how":"pull","target":"self"},{"do":"summon","id":"wolf"},{"do":"light","radius":4,"target":"self"},
        {"do":"surface","id":"ice"},{"do":"flag","id":"x"},{"do":"damage","dice":3,"target":"self"},{"do":"damage","dice":3,"target":"enemies"}])").run(bare, context);
    CHECK(result.events.size() == 1 && bare.one.hp == 97);

    // Who a step lands on.
    host = table();
    host.area = {1, 2, 3};
    effect(R"([{"do":"damage","dice":1,"target":"self"},{"do":"damage","dice":2,"target":"target"},{"do":"damage","dice":4,"target":"area"},
        {"do":"damage","dice":8,"target":"allies"},{"do":"damage","dice":16,"target":"enemies"}])").run(host, context);
    CHECK(host.sheets[0].hp == 100 - 1 - 8 && host.sheets[1].hp == 100 - 4 - 8 && host.sheets[2].hp == 100 - 2 - 4 - 16 && host.sheets[3].hp == 100 - 4 - 16);
    context.targets = {2, 3, 99}; // several targets, and one that is not there
    host = table();
    effect(R"([{"do":"damage","dice":3}])").run(host, context);
    CHECK(host.sheets[2].hp == 97 && host.sheets[3].hp == 97);
    context.targets = {2};

    // Repeat, with a fixed count; choose, through the host.
    host = table();
    result = effect(R"([{"do":"repeat","times":3,"steps":[{"do":"damage","dice":2}]}])").run(host, context);
    CHECK(host.sheets[2].hp == 94 && count(result, Kind::Damage) == 3);
    const yh::Effect either = effect(R"([{"do":"choose","options":[{"name":"Burn","steps":[{"do":"damage","dice":5,"type":"fire"}]},
        {"name":"Mend","steps":[{"do":"heal","dice":3}]}]}])");
    result = either.run(host, context);
    CHECK(host.sheets[2].hp == 89 && result.events[0].kind == Kind::Choice && result.events[0].id == "Burn");
    host.pick = 1;
    result = either.run(host, context);
    CHECK(host.sheets[2].hp == 92 && result.events[0].id == "Mend" && result.events[0].amount == 1);
    host.pick = 7; // a host answering out of range gets the last option
    either.run(host, context);
    CHECK(host.sheets[2].hp == 95);
}

void rolls(const yh::Ruleset& rules)
{
    // An attack: the weapon's bonus against armour class, a natural 1 always missing and a natural
    // 20 always a critical hit that rolls the damage dice twice. Whatever the dice do, the steps
    // under it follow the result.
    const yh::Effect strike = effect(R"([{"do":"roll","kind":"attack","steps":[
        {"do":"damage","dice":"weapon","when":"hit","minimum":1},
        {"do":"condition","id":"shaken","when":"crit"},
        {"do":"condition","id":"burning","when":"miss","target":"self"}]}])");
    int hits = 0, misses = 0, crits = 0;
    bool consistent = true;
    for (uint64_t seed = 1; seed <= 300; seed++)
    {
        TestHost host = table();
        yh::Item sword;
        sword.id = "sword";
        sword.slot = "mainHand";
        sword.damage = "1d8";
        host.sheets[0].inventory.push_back(sword);
        host.sheets[0].equip(0);
        host.sheets[0].stats.setBase("str", 14);
        host.sheets[0].addCondition(rules, "unseen");
        host.sheets[2].stats.setBase("ac", 13);
        yh::Random random(seed);
        yh::EffectContext context;
        context.rules = &rules;
        context.random = &random;
        context.self = 0;
        context.targets = {2};
        const yh::EffectResult result = strike.run(host, context);
        const yh::EffectEvent* attack = first(result, Kind::Attack);
        const yh::EffectEvent* damage = first(result, Kind::Damage);
        if (!attack || result.events.empty() || result.events[0].kind != Kind::Attack)
        {
            consistent = false;
            continue;
        }
        const bool hit = !attack->roll.natural1() && (attack->roll.natural20() || attack->roll.total >= 13);
        consistent = consistent && attack->success == hit && attack->critical == attack->roll.natural20() && attack->dc == 13
            && attack->roll.flat == 2 && (damage != nullptr) == hit && !host.sheets[0].hasCondition("unseen")
            && result.events[1].kind == Kind::ConditionEnded && result.events[1].who == 0
            && host.sheets[2].hasCondition("shaken") == attack->critical && host.sheets[0].hasCondition("burning") == !hit;
        if (damage)
        {
            const size_t dice = static_cast<size_t>(std::count_if(damage->roll.dice.begin(), damage->roll.dice.end(), [](const yh::DieRoll& d) { return d.sides == 8; }));
            consistent = consistent && dice == (attack->critical ? 2u : 1u) && damage->roll.flat == 2 && damage->critical == attack->critical
                && host.sheets[2].hp == 100 - damage->amount && damage->amount == damage->roll.total;
        }
        hits += hit && !attack->critical;
        misses += !hit;
        crits += attack->critical;
    }
    CHECK(consistent);
    CHECK(hits > 0 && misses > 0 && crits > 0);

    // The same dice as the encounter's own attack, in the same order, so a fight can move onto effects unchanged.
    for (uint64_t seed = 1; seed <= 40; seed++)
    {
        TestHost host = table();
        host.sheets[0].stats.setBase("str", 16);
        yh::Character attacker = host.sheets[0], defender = host.sheets[2];
        yh::Encounter fight(rules, seed);
        fight.add(attacker, 0);
        fight.add(defender, 1);
        fight.start();
        const size_t me = fight.currentIndex();
        const yh::AttackResult expected = fight.attack(1 - me);
        // The effect, from a copy of the encounter's dice as they stood before that attack.
        yh::Encounter again(rules, seed);
        yh::Character attackerAgain = host.sheets[0], defenderAgain = host.sheets[2];
        again.add(attackerAgain, 0);
        again.add(defenderAgain, 1);
        again.start();
        yh::EffectContext context;
        context.rules = &rules;
        context.random = &again.random();
        context.self = again.order()[me].character == &attackerAgain ? 0 : 2;
        context.targets = {context.self == 0 ? 2 : 0};
        host.sheets[0] = attackerAgain;
        host.sheets[2] = defenderAgain;
        const yh::EffectResult result = strike.run(host, context);
        const yh::EffectEvent* attack = first(result, Kind::Attack);
        const yh::EffectEvent* damage = first(result, Kind::Damage);
        CHECK(attack && attack->roll.describe() == expected.attackRoll.describe() && attack->success == expected.hit && attack->critical == expected.critical);
        CHECK((damage != nullptr) == expected.hit && (!damage || damage->roll.describe() == expected.damageRoll.describe()));
    }

    yh::Random random(3);
    yh::EffectContext context;
    context.rules = &rules;
    context.random = &random;
    context.self = 0;
    context.targets = {2};
    context.dc = 1000;

    // A check by the doer against a DC, the caster's DC or the target's passive score.
    TestHost host = table();
    const char* checkJson = R"([{"do":"roll","kind":"check","ability":"athletics","dc":%s,"target":"target","steps":[
        {"do":"condition","id":"shaken","when":"success"},{"do":"condition","id":"burning","when":"failure","target":"self"}]}])";
    char text[512];
    std::snprintf(text, sizeof text, checkJson, "-100");
    yh::EffectResult result = effect(text).run(host, context);
    CHECK(host.sheets[2].hasCondition("shaken") && !host.sheets[0].hasCondition("burning") && result.events[0].kind == Kind::Check
        && result.events[0].success && result.events[0].dc == -100 && result.events[0].id == "athletics");
    host = table();
    std::snprintf(text, sizeof text, checkJson, "\"caster\"");
    result = effect(text).run(host, context);
    CHECK(!host.sheets[2].hasCondition("shaken") && host.sheets[0].hasCondition("burning") && !result.events[0].success && result.events[0].dc == 1000);
    host = table();
    host.sheets[2].stats.setBase("str", 18);
    result = effect(R"([{"do":"roll","kind":"check","ability":"athletics","against":"athletics","steps":[]}])").run(host, context);
    CHECK(result.events.size() == 1 && result.events[0].who == 2 && result.events[0].dc == 14); // 10 + 4
    result = effect(R"([{"do":"roll","kind":"check","ability":"str","dc":5,"steps":[{"do":"heal","dice":1}]}])").run(host, context);
    CHECK(result.events[0].who == 0); // with nobody to beat, the check is about the doer

    // A save made by each target, gating the steps under it.
    host = table();
    context.targets = {2, 3};
    result = effect(R"([{"do":"roll","kind":"save","ability":"dex","dc":1000,"steps":[
        {"do":"damage","dice":10,"onSave":"half"},{"do":"condition","id":"burning","when":"saveFailed"},{"do":"condition","id":"shaken","when":"saveSucceeded"}]}])").run(host, context);
    CHECK(host.sheets[2].hp == 90 && host.sheets[3].hp == 90 && host.sheets[2].hasCondition("burning") && !host.sheets[3].hasCondition("shaken")
        && count(result, Kind::Save) == 2 && !result.events[0].success && result.events[0].who == 2);
    host = table();
    result = effect(R"([{"do":"roll","kind":"save","ability":"dex","dc":"caster","steps":[{"do":"damage","dice":10,"onSave":"none"}]},
        {"do":"roll","kind":"save","ability":"dex","dc":-100,"steps":[
        {"do":"damage","dice":9,"onSave":"half"},{"do":"damage","dice":50,"onSave":"none"},{"do":"condition","id":"burning","when":"saveFailed"},
        {"do":"condition","id":"shaken","when":"saveSucceeded"}]}])").run(host, context);
    CHECK(host.sheets[2].hp == 100 - 10 - 4 && host.sheets[3].hp == 100 - 10 - 4 && !host.sheets[2].hasCondition("burning") && host.sheets[3].hasCondition("shaken"));
}

void savesAndScaling(const yh::Ruleset& rules)
{
    yh::Random random(5);
    yh::EffectContext context;
    context.rules = &rules;
    context.random = &random;
    context.self = 0;
    context.targets = {2};

    // The effect's own save: each creature makes it once, however many steps ask; a success halves
    // or stops a step as the step says, and steps that do not mention it ignore it.
    const char* blast = R"({"save":{"ability":"dex","dc":%s},"effects":[
        {"do":"damage","dice":11,"type":"fire","onSave":"half","target":"area"},
        {"do":"condition","id":"burning","onSave":"none","target":"area"},
        {"do":"condition","id":"shaken","when":"saveSucceeded","target":"area"},
        {"do":"damage","dice":1,"target":"area"}]})";
    char text[512];
    TestHost host = table();
    host.area = {2, 3};
    std::snprintf(text, sizeof text, blast, "-100");
    yh::EffectResult result = effect(text).run(host, context);
    CHECK(host.sheets[2].hp == 100 - 5 - 1 && host.sheets[3].hp == 100 - 5 - 1 && !host.sheets[2].hasCondition("burning") && host.sheets[3].hasCondition("shaken"));
    CHECK(count(result, Kind::Save) == 2 && result.events[0].kind == Kind::Save && result.events[0].success && result.events[0].who == 2
        && result.events[0].id == "dex" && first(result, Kind::Damage)->amount == 5);
    host = table();
    host.area = {2, 3};
    std::snprintf(text, sizeof text, blast, "1000");
    result = effect(text).run(host, context);
    CHECK(host.sheets[2].hp == 100 - 11 - 1 && host.sheets[2].hasCondition("burning") && !host.sheets[2].hasCondition("shaken") && count(result, Kind::Save) == 2);
    host = table();
    host.area = {2};
    context.dc = -100;
    std::snprintf(text, sizeof text, blast, "\"caster\"");
    effect(text).run(host, context);
    CHECK(host.sheets[2].hp == 94 && host.sheets[1].hp == 100);
    // One roll of the damage for everyone in the area.
    host = table();
    host.area = {1, 2, 3};
    result = effect(R"([{"do":"damage","dice":"4d6","target":"area"}])").run(host, context);
    CHECK(count(result, Kind::Damage) == 3 && host.sheets[1].hp == host.sheets[2].hp && host.sheets[2].hp == host.sheets[3].hp && host.sheets[1].hp < 100);
    // Nobody in a save-for-nothing effect rolls if no step asks.
    result = effect(R"({"save":{"ability":"dex","dc":10},"effects":[{"do":"damage","dice":1}]})").run(host, context);
    CHECK(count(result, Kind::Save) == 0);

    // Scaling: once for each `every` levels above `from`, by the doer's level or the slot used.
    const yh::Effect byLevel = effect(R"([{"do":"damage","dice":10,"scale":{"by":"level","from":1,"every":2,"dice":"3","value":1}}])");
    host = table();
    byLevel.run(host, context);
    CHECK(host.sheets[2].hp == 90); // level 1: nothing added
    host.sheets[2].hp = 100;
    host.sheets[0].level = 5;
    byLevel.run(host, context);
    CHECK(host.sheets[2].hp == 100 - 10 - 2 * 4);
    host.sheets[2].hp = 100;
    host.sheets[0].level = 6; // not yet a third step
    byLevel.run(host, context);
    CHECK(host.sheets[2].hp == 100 - 18);
    host.sheets[2].hp = 100;
    context.level = 9; // the caller can say what level it is done at
    byLevel.run(host, context);
    CHECK(host.sheets[2].hp == 100 - 10 - 4 * 4);
    context.level = 0;
    const yh::Effect bySlot = effect(R"([{"do":"heal","dice":4,"scale":{"by":"slot","from":1,"dice":"2"}},
        {"do":"condition","id":"shaken","scale":{"by":"slot","from":1,"value":1}},
        {"do":"repeat","times":1,"scale":{"by":"slot","from":2,"value":1},"steps":[{"do":"tempHp","dice":1,"target":"self"},{"do":"damage","dice":1,"target":"self"}]}])");
    host = table();
    host.sheets[2].hp = 50;
    context.slot = 1;
    bySlot.run(host, context);
    CHECK(host.sheets[2].hp == 54 && host.sheets[2].conditionValue("shaken") == 1 && host.sheets[0].hp == 100 && host.sheets[0].tempHp == 0);
    host = table();
    host.sheets[2].hp = 50;
    context.slot = 3;
    result = bySlot.run(host, context);
    CHECK(host.sheets[2].hp == 58 && host.sheets[2].conditionValue("shaken") == 3 && count(result, Kind::TempHp) == 2);
    const yh::EffectEvent* rolled = first(result, Kind::Heal);
    CHECK(rolled && rolled->roll.total == 8);

    // Steps that wait for an event run only when the effect is run for that event.
    const yh::Effect lingering = effect(R"([{"do":"damage","dice":6},{"do":"damage","dice":2,"when":"turnStart"},
        {"do":"repeat","times":1,"when":"turnEnd","steps":[{"do":"heal","dice":1}]}])");
    host = table();
    context.slot = 0;
    lingering.run(host, context);
    CHECK(host.sheets[2].hp == 94);
    context.event = "turnStart";
    lingering.run(host, context);
    CHECK(host.sheets[2].hp == 92);
    context.event = "turnEnd";
    lingering.run(host, context);
    CHECK(host.sheets[2].hp == 93);

    // Without rules or dice there is nothing to run.
    yh::EffectContext nothing;
    CHECK(lingering.run(host, nothing).events.empty() && host.sheets[2].hp == 93);
}

}

void effects()
{
    std::string error;
    const std::optional<yh::Ruleset> rules = yh::Ruleset::fromJson(authored, &error);
    CHECK(rules && error.empty());
    if (!rules)
        return;
    files(*rules);
    stepKinds(*rules);
    rolls(*rules);
    savesAndScaling(*rules);

    yh::Ruleset flagged = *rules;
    flagged.conditions.push_back(*yh::ConditionDefinition::fromJson(R"({"id":"wounded","flags":["wounded"]})"));
    TestHost host = table();
    host.sheets[2].hp = 50;
    yh::Random random(8);
    yh::EffectContext context;
    context.rules = &flagged;
    context.random = &random;
    context.self = 0;
    context.targets = {2, 99};
    const yh::Effect help = effect(R"([{"do":"heal","dice":1,"ifFlag":"wounded"}])");
    CHECK(help.run(host, context).events.empty() && host.sheets[2].hp == 50);
    host.sheets[2].addCondition(flagged, "wounded");
    CHECK(help.run(host, context).events.size() == 1 && host.sheets[2].hp == 51);
}

void actions()
{
    namespace fs = std::filesystem;
    std::string error;
    const std::optional<yh::Ruleset> parsedRules = yh::Ruleset::fromJson(authored, &error);
    CHECK(parsedRules && error.empty());
    if (!parsedRules)
        return;
    yh::Ruleset rules = *parsedRules;
    rules.actionsPerTurn = 2;

    // One file: cost, requirements, targeting and effects.
    const auto trip = yh::ActionDefinition::fromJson(R"({"id":"trip","name":"Trip","description":"Knock it down.","order":30,"cost":2,
        "requires":{"flags":["armed"],"without":["cantAct"],"resources":{"grit":1}},
        "target":{"kind":"creature","side":"any","range":2},"log":"{name} sweeps low",
        "save":{"ability":"dex","dc":12},
        "effects":[{"do":"condition","id":"shaken","onSave":"none"}]})", &error);
    CHECK(trip && error.empty());
    if (!trip)
        return;
    CHECK(trip->name == "Trip" && trip->order == 30 && trip->cost == 2 && !trip->costsHands && !trip->endsTurn && trip->general);
    CHECK(trip->needsFlags == std::vector<std::string>{"armed"} && trip->barredBy == std::vector<std::string>{"cantAct"}
        && trip->needsResources.size() == 1 && trip->needsResources[0].second == 1);
    CHECK(trip->target == yh::ActionDefinition::Target::Creature && trip->side == yh::ActionDefinition::Side::Any && trip->range == 2
        && trip->log == "{name} sweeps low" && trip->effect.steps.size() == 1 && trip->effect.save.dc == 12 && trip->effect.check(rules));
    const auto again = yh::ActionDefinition::fromJson(trip->json);
    CHECK(again && again->json == trip->json);
    const auto bare = yh::ActionDefinition::fromJson(R"({"id":"wait"})");
    CHECK(bare && bare->name == "wait" && bare->cost == 1 && bare->target == yh::ActionDefinition::Target::Self && bare->effect.empty() && bare->general);

    auto refused = [&](const char* json, const char* field) {
        return !yh::ActionDefinition::fromJson(json, &error) && error.find(field) != std::string::npos;
    };
    CHECK(refused(R"({"name":"No id"})", "id"));
    CHECK(refused(R"({"id":"x","cost":"lots"})", "cost") && refused(R"({"id":"x","cost":11})", "cost"));
    CHECK(refused(R"({"id":"x","price":1})", "price"));
    CHECK(refused(R"({"id":"x","target":{"kind":"cone"}})", "target.kind"));
    CHECK(refused(R"({"id":"x","target":{"kind":"creature","side":"both"}})", "target.side"));
    CHECK(refused(R"({"id":"x","target":{"kind":"creature","range":0}})", "target.range"));
    CHECK(refused(R"({"id":"x","target":{"kind":"self","range":3}})", "target"));
    CHECK(refused(R"({"id":"x","requires":{"flags":"armed"}})", "requires.flags"));
    CHECK(refused(R"({"id":"x","requires":{"resources":{"grit":0}}})", "requires.resources.grit"));
    CHECK(refused(R"({"id":"x","requires":{"mood":"good"}})", "requires.mood"));
    CHECK(refused(R"({"id":"x","effects":[{"do":"heal","dice":1},{"do":"explode"}]})", "effects[1].do"));
    CHECK(refused(R"({"id":"x","endsTurn":"yes"})", "endsTurn"));
    CHECK(refused(R"({"id":"x","target":{"kind":"creature","downed":1}})", "target.downed"));
    const auto help = yh::ActionDefinition::fromJson(R"({"id":"help","target":{"kind":"creature","side":"ally","downed":true}})");
    const auto ready = yh::ActionDefinition::fromJson(R"({"id":"ready","readies":"strike","endsTurn":true})");
    CHECK(help && help->allowsDowned && ready && ready->readies == "strike" && ready->endsTurn);

    // What it costs and whether a creature may: hands of the weapon in use, flags and resources.
    yh::Character c = plain("Ana");
    const auto swing = yh::ActionDefinition::fromJson(R"({"id":"swing","cost":"hands"})");
    CHECK(swing && swing->costsHands && swing->costFor(c, rules) == 1); // unarmed
    yh::Item axe;
    axe.id = "axe";
    axe.slot = "mainHand";
    axe.hands = 2;
    c.inventory.push_back(axe);
    c.equip(0);
    CHECK(swing->costFor(c, rules) == 2 && trip->costFor(c, rules) == 2);
    c.inventory[0].hands = 5;
    CHECK(swing->costFor(c, rules) == 2); // never more than a turn has
    std::string why;
    yh::Ruleset flagged = rules;
    flagged.conditions.push_back(*yh::ConditionDefinition::fromJson(R"({"id":"ready","flags":["armed"]})"));
    flagged.conditions.push_back(*yh::ConditionDefinition::fromJson(R"({"id":"stunned","flags":["cantAct"]})"));
    CHECK(!trip->meets(c, flagged, &why) && why.find("armed") != std::string::npos);
    c.addCondition(flagged, "ready");
    CHECK(!trip->meets(c, flagged, &why) && why.find("grit") != std::string::npos);
    c.resources["grit"] = {1, 1};
    CHECK(trip->meets(c, flagged, &why) && why.empty());
    c.addCondition(flagged, "stunned");
    CHECK(!trip->meets(c, flagged, &why) && why.find("cantAct") != std::string::npos);
    CHECK(bare->meets(c, flagged));

    // The three every fight has, costing what the ruleset says a strike costs.
    std::vector<yh::ActionDefinition> basic = yh::basicActions(rules);
    CHECK(basic.size() == 3 && basic[0].id == "strike" && basic[1].id == "stride" && basic[2].id == "end-turn");
    CHECK(!basic[0].costsHands && basic[0].cost == 1 && basic[0].target == yh::ActionDefinition::Target::Creature && basic[0].range == 1
        && basic[1].cost == 1 && basic[1].log == "{name} dashes" && basic[2].cost == 0 && basic[2].endsTurn);
    CHECK(basic[0].effect.check(rules) && basic[1].effect.check(rules));
    rules.strikeCostsHands = true;
    CHECK(yh::basicActions(rules)[0].costsHands);
    CHECK(yh::findAction(basic, "stride") == &basic[1] && !yh::findAction(basic, "fly"));

    // A folder of them, one file per action, over what is already there.
    const fs::path root = fs::temp_directory_path() / "yorehold-action-test";
    fs::remove_all(root);
    auto write = [&](const char* file, const char* text) {
        fs::create_directories((root / file).parent_path());
        std::ofstream(root / file, std::ios::binary) << text;
    };
    write("good/actions/stride.json", R"({"id":"stride","name":"Stride","order":5,"cost":1})");
    write("good/actions/shout.json", R"({"id":"shout","name":"Shout","order":7,"cost":0,"general":false,"effects":[{"do":"condition","id":"shaken","target":"enemies"}]})");
    write("good/actions/notes.txt", "not an action");
    write("misnamed/actions/shout.json", R"({"id":"yell"})");
    write("broken/actions/shout.json", R"({"id":"shout","effects":[{"do":"sing"}]})");
    write("dangling/actions/shout.json", R"({"id":"shout","effects":[{"do":"condition","id":"deafened"}]})");
    write("bad-ready/actions/ready.json", R"({"id":"ready","readies":"absent"})");
    yh::FileSystem disk;
    CHECK(disk.mountFolder(root.string(), "test"));
    std::vector<yh::ActionDefinition> loaded = basic;
    CHECK(yh::loadActions(disk, "good/actions", rules, loaded, &error) && error.empty());
    CHECK(loaded.size() == 4 && loaded[0].id == "stride" && loaded[0].name == "Stride" && loaded[1].id == "shout" && !loaded[1].general
        && loaded[2].id == "strike" && loaded[3].id == "end-turn");
    CHECK(!yh::loadActions(disk, "misnamed/actions", rules, loaded, &error) && error.find("misnamed/actions/shout.json") != std::string::npos);
    CHECK(!yh::loadActions(disk, "broken/actions", rules, loaded, &error) && error.find("broken/actions/shout.json") != std::string::npos
        && error.find("effects[0].do") != std::string::npos && error.find("sing") != std::string::npos);
    CHECK(!yh::loadActions(disk, "dangling/actions", rules, loaded, &error) && error.find("deafened") != std::string::npos);
    CHECK(!yh::loadActions(disk, "bad-ready/actions", rules, loaded, &error) && error.find("ready.json") != std::string::npos
        && error.find("readies") != std::string::npos);
    CHECK(loaded.size() == 4 && loaded[1].name == "Shout"); // a failed load changes nothing
    CHECK(yh::loadActions(disk, "missing/actions", rules, loaded) && loaded.size() == 4); // no folder, no more actions
    disk.unmount("test");
    fs::remove_all(root);
}

}
