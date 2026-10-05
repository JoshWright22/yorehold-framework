#include "StashTests.h"
#include "Checks.h"

#include <yorehold/framework/rpg/Compendium.h>
#include <yorehold/framework/rpg/Stash.h>

namespace regression
{
namespace
{

yh::Item food(const char* id, int supplies, int quantity = 1)
{
    yh::Item item; item.id = id; item.name = id; item.slot = ""; item.hands = 0;
    item.supplies = supplies; item.quantity = quantity;
    return item;
}

yh::Item sword()
{
    yh::Item item; item.id = "sword"; item.name = "Sword"; item.slot = "mainHand"; item.damage = "1d8";
    return item;
}

void stacking()
{
    yh::Stash stash;
    stash.add(food("bread", 5, 2));
    stash.add(food("bread", 5, 3));
    CHECK(stash.items.size() == 1 && stash.items[0].quantity == 5);
    stash.add(sword());
    stash.add(sword());
    CHECK(stash.items.size() == 3);
    yh::Item worn = sword(); worn.equipped = true;
    stash.add(worn);
    CHECK(!stash.items.back().equipped);
    yh::Item stale = food("bread", 7); // same id, different worth: its own entry
    stash.add(stale);
    CHECK(stash.items.size() == 5);
    stash.add(food("bread", 5, 0)); // nothing to add
    CHECK(stash.items.size() == 5 && stash.supplies() == 5 * 5 + 7);

    const auto two = stash.take(0, 2);
    CHECK(two && two->quantity == 2 && stash.items[0].quantity == 3);
    const auto rest = stash.take(0, 0);
    CHECK(rest && rest->quantity == 3 && stash.items.size() == 4 && stash.items[0].id == "sword");
    CHECK(!stash.take(99));
}

void saving()
{
    yh::Stash stash;
    stash.add(food("bread", 5, 4));
    stash.add(sword());
    std::string error;
    const auto back = yh::Stash::fromJson(stash.toJson(), &error);
    CHECK(back && back->items.size() == 2 && back->items[0].quantity == 4 && back->items[0].supplies == 5
        && back->items[1].damage == "1d8" && back->supplies() == 20);
    CHECK(yh::Stash::fromJson("{}") && yh::Stash::fromJson("{}")->empty());
    CHECK(!yh::Stash::fromJson("[]", &error) && !error.empty());
    CHECK(!yh::Stash::fromJson(R"({"items":[{"id":"","quantity":1}]})", &error) && !error.empty());
    CHECK(!yh::Stash::fromJson(R"({"items":[{"id":"bread","quantity":0}]})"));

    // Item files carry supplies; zero leaves the field out so older files read the same.
    const auto parsed = yh::Compendium::itemFromJson(R"({"id":"bread","slot":"","hands":0,"supplies":10})");
    CHECK(parsed && parsed->supplies == 10);
    CHECK(yh::Compendium::itemToJson(sword()).find("supplies") == std::string::npos);
    CHECK(!yh::Compendium::itemFromJson(R"({"id":"bread","supplies":-1})"));
    yh::Character carrier; carrier.inventory.push_back(food("bread", 3));
    const auto sheet = yh::Character::fromJson(carrier.toJson());
    CHECK(sheet && sheet->inventory.size() == 1 && sheet->inventory[0].supplies == 3);
}

void spending()
{
    yh::Stash stash;
    stash.add(food("feast", 20));
    stash.add(food("bread", 5, 2));
    yh::Character ana, bo;
    ana.inventory.push_back(food("apple", 2, 3));
    yh::Item packed = food("ration", 10); packed.slot = "belt"; packed.equipped = true; // worn: not food for the camp
    bo.inventory.push_back(packed);
    bo.inventory.push_back(food("ration", 10, 2));

    CHECK(yh::supplyPoints(stash, {&ana, &bo}) == 30 + 6 + 20);
    CHECK(!yh::spendSupplies(stash, {&ana, &bo}, 57));
    CHECK(stash.supplies() == 30 && ana.inventory[0].quantity == 3); // a refusal changes nothing
    CHECK(yh::spendSupplies(stash, {&ana, &bo}, 0));

    // The stash goes first, cheapest units first: two breads, then the feast.
    CHECK(yh::spendSupplies(stash, {&ana, &bo}, 10));
    CHECK(stash.items.size() == 1 && stash.items[0].id == "feast");
    // An uneven cost takes the whole unit.
    CHECK(yh::spendSupplies(stash, {&ana, &bo}, 15) && stash.empty());
    // Then the sheets in order.
    CHECK(yh::spendSupplies(stash, {&ana, &bo}, 8));
    CHECK(ana.inventory.empty() && bo.inventory.size() == 2 && bo.inventory[1].quantity == 1 && bo.inventory[0].equipped);
    CHECK(yh::supplyPoints(stash, {&ana, &bo}) == 10);
}

void revival()
{
    auto rules = yh::Ruleset::modern(); rules.death.enabled = true; rules.death.deadCondition = "dead";
    yh::ConditionDefinition dead; dead.id = "dead"; dead.name = "Dead"; dead.flags = {"cantAct"};
    rules.conditions.push_back(dead);
    yh::Character c; c.name = "Ana"; c.stats.setBase("maxHp", 20); c.hp = 5;
    CHECK(!c.revive(rules, 1) && c.hp == 5);
    c.hp = 0; c.death.dead = true; c.death.failures = 3; c.syncDeath(rules);
    CHECK(c.hasCondition("dead"));
    CHECK(c.revive(rules, 0) && c.hp == 20 && !c.death.dead && c.death.failures == 0 && !c.hasCondition("dead"));
    c.hp = 0; c.death.dead = true;
    CHECK(c.revive(rules, 1) && c.hp == 1);
    c.hp = 0; c.death.dead = true;
    CHECK(c.revive(rules, 500) && c.hp == 20);

    // Rest costs, camp-only rests and the price of revival are ruleset data.
    std::string error;
    const std::string head = R"({"id":"r","name":"R","abilities":[{"id":"str","name":"Str"},{"id":"dex","name":"Dex"},{"id":"con","name":"Con"}],)";
    CHECK(yh::Ruleset::fromJson(head + R"("rests":[{"id":"long"}]})"));
    auto loaded = yh::Ruleset::fromJson(head + R"("rests":[{"id":"short","perAdventure":2},{"id":"long","supplyCost":40,"campOnly":true,"resets":["short"]}],
        "revivePrice":20000,"reviveHp":1})", &error);
    CHECK(loaded && loaded->rest("long")->supplyCost == 40 && loaded->rest("long")->campOnly
        && loaded->rest("long")->resets == std::vector<std::string>{"short"} && loaded->rest("short")->resets.empty()
        && loaded->rest("short")->supplyCost == 0 && !loaded->rest("short")->campOnly
        && loaded->revivePrice == 20000 && loaded->reviveHp == 1);
    const auto again = loaded ? yh::Ruleset::fromJson(loaded->toJson()) : std::nullopt;
    CHECK(again && again->rest("long")->supplyCost == 40 && again->rest("long")->campOnly && again->revivePrice == 20000
        && again->rest("long")->resets.size() == 1);
    CHECK(!yh::Ruleset::fromJson(head + R"("rests":[{"id":"long","resets":["nap"]}]})"));
    CHECK(!yh::Ruleset::fromJson(head + R"("rests":[{"id":"long","supplyCost":-1}]})"));
    CHECK(!yh::Ruleset::fromJson(head + R"("revivePrice":-5})"));
}

}

void stashAndSupplies()
{
    stacking();
    saving();
    spending();
    revival();
}

}
