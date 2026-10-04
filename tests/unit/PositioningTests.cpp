#include "PositioningTests.h"
#include "Checks.h"

#include <yorehold/framework/graphics/Lighting.h>
#include <yorehold/framework/map/Positioning.h>
#include <yorehold/framework/rpg/Effect.h>
#include <yorehold/framework/rpg/PositioningRules.h>

namespace regression
{

void positioning()
{
    yh::Grid grid(yh::GridType::Square, 10);
    std::vector<yh::Cell> foes{{-1, 0}, {1, 0}};
    CHECK(yh::isFlanked(grid, {0, 0}, foes));
    foes = {{-1, -1}, {1, 1}};
    CHECK(yh::isFlanked(grid, {0, 0}, foes));
    foes = {{-1, 0}, {0, 1}};
    CHECK(!yh::isFlanked(grid, {0, 0}, foes));
    foes = {{-2, 0}, {2, 0}};
    CHECK(!yh::isFlanked(grid, {0, 0}, foes) && yh::isFlanked(grid, {0, 0}, foes, 2));
    CHECK(!yh::isFlanked(grid, {0, 0}, foes, 2, [](yh::Vec2 a, yh::Vec2) { return a.x < 0; }));
    foes = {{0, 0}, {1, 0}, {1, 0}};
    CHECK(!yh::isFlanked(grid, {0, 0}, foes));
    yh::Grid hex(yh::GridType::Hex, 10);
    foes = {{-1, 1}, {1, -1}};
    CHECK(yh::isFlanked(hex, {0, 0}, foes));
    yh::Grid free(yh::GridType::Gridless, 10);
    CHECK(yh::isFlanked(free, {0, 0}, foes, 2));

    auto behind = [&](yh::Wall wall) {
        return yh::coverBetween(grid, {0, 0}, {4, 0}, [wall](yh::Vec2 a, yh::Vec2 b) {
            return !yh::lineOfSight(a, b, std::span<const yh::Wall>(&wall, 1));
        });
    };
    CHECK(behind({{35, 15}, {35, 20}}) == yh::Cover::None);
    CHECK(behind({{35, 5}, {35, 20}}) == yh::Cover::Half);
    CHECK(behind({{35, 2}, {35, 20}}) == yh::Cover::ThreeQuarters);
    CHECK(behind({{35, 0}, {35, 20}}) == yh::Cover::Full);
    CHECK(yh::coverBetween(grid, {0, 0}, {0, 0}, [](yh::Vec2, yh::Vec2) { return true; }) == yh::Cover::None);
    CHECK(yh::coverBetween(hex, {0, 0}, {4, 0}, [](yh::Vec2, yh::Vec2) { return true; }) == yh::Cover::Full);

    yh::Ruleset rules = yh::Ruleset::modern();
    yh::ConditionDefinition guard; guard.id = "guard";
    rules.conditions.push_back(guard);
    const auto config = yh::PositioningRules::fromJson(R"({"flankingCondition":"guard","halfCoverArmorClass":2,"threeQuartersCoverArmorClass":4})");
    CHECK(config && config->enabled && config->check(rules) && config->halfCoverArmorClass == 2);
    CHECK(config && yh::PositioningRules::fromJson(config->toJson())->toJson() == config->toJson());
    CHECK(!yh::PositioningRules{}.enabled);
    CHECK(!yh::PositioningRules::fromJson(R"({"flankingReach":0})"));
    CHECK(!yh::PositioningRules::fromJson(R"({"halfCoverArmorClass":5,"threeQuartersCoverArmorClass":4})"));
    CHECK(!yh::PositioningRules::fromJson(R"({"creaturesProvideCover":"yes"})"));
    CHECK(!yh::PositioningRules::fromJson(R"({"halfCover":2})"));
    const auto unknown = yh::PositioningRules::fromJson(R"({"flankingCondition":"missing"})");
    std::string problem;
    CHECK(unknown && !unknown->check(rules, &problem) && problem.find("missing") != std::string::npos);

    struct Host : yh::EffectHost
    {
        yh::Character creature;
        bool positional = true;
        yh::Character* sheet(yh::EffectActor) override { return &creature; }
        std::vector<yh::EffectActor> group(std::string_view, const yh::EffectContext&) override { return {}; }
        int armorClass(yh::EffectActor, const yh::EffectContext&) override { return 123; }
        bool hasFlag(yh::EffectActor, std::string_view flag, const yh::EffectContext&) override { return positional && flag == "positioned"; }
    } host;
    host.creature.stats.setBase("maxHp", 100); host.creature.hp = 100;
    yh::Random random(7);
    yh::EffectContext context; context.rules = &rules; context.random = &random; context.self = 0; context.targets = {0};
    const auto effect = yh::Effect::fromJson(R"([{"do":"roll","kind":"attack","steps":[]},{"do":"damage","dice":3,"ifFlag":"positioned"}])");
    CHECK(effect && effect->run(host, context).events[0].dc == 123 && host.creature.hp == 97);
    host.positional = false;
    effect->run(host, context);
    CHECK(host.creature.hp == 97);
    CHECK(host.yh::EffectHost::armorClass(0, context) == host.creature.armorClass(rules)
        && !host.yh::EffectHost::hasFlag(0, "positioned", context));
}

}
