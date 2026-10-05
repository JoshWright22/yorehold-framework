#include "CompanionTests.h"
#include "Checks.h"

#include <yorehold/framework/rpg/Companions.h>

#include <nlohmann/json.hpp>

namespace regression
{
namespace
{

yh::CompanionDefinition friendOf(const char* id, int joinAt = 0, std::optional<int> leaveAt = std::nullopt)
{
    yh::CompanionDefinition d;
    d.id = id;
    d.joinAt = joinAt;
    d.leaveAt = leaveAt;
    return d;
}

void joiningAndCaps()
{
    yh::CompanionRules rules;
    rules.limit = 2;
    rules.partyLimit = 5;
    yh::Companions c;
    c.define(friendOf("wren", 10));
    c.define(friendOf("bran"));
    c.define(friendOf("ash"));
    CHECK(c.join(rules, "nobody", 1) == yh::CompanionJoin::Unknown);
    CHECK(c.join(rules, "wren", 1) == yh::CompanionJoin::LowApproval && !c.member("wren"));
    c.adjust(rules, "wren", 10);
    CHECK(c.approval("wren") == 10);
    CHECK(c.join(rules, "wren", 1) == yh::CompanionJoin::Joined && c.member("wren"));
    CHECK(c.join(rules, "wren", 1) == yh::CompanionJoin::Already);
    // Four players and one companion fill a party of five.
    CHECK(c.canJoin(rules, "bran", 4) == yh::CompanionJoin::Full);
    CHECK(c.join(rules, "bran", 3) == yh::CompanionJoin::Joined);
    // Two companions is the limit however small the party.
    CHECK(c.canJoin(rules, "ash", 1) == yh::CompanionJoin::Full);
    CHECK(c.leave("bran") && !c.leave("bran"));
    CHECK(c.join(rules, "ash", 1) == yh::CompanionJoin::Joined);
    CHECK(c.members().size() == 2 && c.members()[0] == "wren" && c.members()[1] == "ash");

    yh::CompanionRules open; // no limits
    yh::Companions many;
    for (const char* id : {"a", "b", "c", "d"})
    {
        many.define(friendOf(id));
        CHECK(many.join(open, id, 6) == yh::CompanionJoin::Joined);
    }
}

void approvalMoves()
{
    yh::CompanionRules rules;
    rules.approvalMin = -20;
    rules.approvalMax = 20;
    yh::Companions c;
    yh::CompanionDefinition wren = friendOf("wren", 0, -10);
    wren.approval = 5;
    wren.flags = {{"spared-goblin", 4}, {"burned-mill", -30}};
    c.define(wren);
    CHECK(c.approval("wren") == 5);
    c.adjust(rules, "wren", 100);
    CHECK(c.approval("wren") == 20);
    c.define(wren); // defining again keeps what they think now
    CHECK(c.approval("wren") == 20);
    CHECK(!c.adjust(rules, "nobody", 3) && c.approval("nobody") == 0);
    CHECK(c.join(rules, "wren", 4) == yh::CompanionJoin::Joined);

    // Each flag counts once, however often the story is looked at.
    c.setApproval(rules, "wren", 0);
    CHECK(c.flagsSet(rules, {"spared-goblin", "other"}).empty() && c.approval("wren") == 4);
    CHECK(c.flagsSet(rules, {"spared-goblin"}).empty() && c.approval("wren") == 4);
    // Falling to leaveAt sends a member away; the range still holds.
    const std::vector<std::string> left = c.flagsSet(rules, {"spared-goblin", "burned-mill"});
    CHECK(left.size() == 1 && left[0] == "wren" && !c.member("wren") && c.approval("wren") == -20);
    CHECK(c.canJoin(rules, "wren", 1) == yh::CompanionJoin::LowApproval);
    c.setApproval(rules, "wren", 0);
    CHECK(c.join(rules, "wren", 1) == yh::CompanionJoin::Joined);
}

void saving()
{
    yh::CompanionRules rules;
    yh::Companions c;
    yh::CompanionDefinition wren = friendOf("wren", 3, -5);
    wren.flags = {{"met", 2}};
    c.define(wren);
    c.define(friendOf("bran"));
    c.flagsSet(rules, {"met"});
    c.adjust(rules, "wren", 1);
    c.join(rules, "wren", 2);
    std::string error;
    const auto back = yh::Companions::fromJson(c.toJson(), &error);
    CHECK(back && back->approval("wren") == 3 && back->member("wren") && !back->member("bran") && back->definitions().size() == 2);
    CHECK(back && back->definition("wren")->leaveAt == -5 && back->definition("wren")->joinAt == 3
        && back->definition("wren")->flags.size() == 1);
    if (!back)
        return;
    yh::Companions again = *back;
    CHECK(again.flagsSet(rules, {"met"}).empty() && again.approval("wren") == 3); // already counted

    CHECK(yh::Companions::fromJson("{}") && yh::Companions::fromJson("{}")->members().empty());
    CHECK(!yh::Companions::fromJson(R"({"members":["ghost"]})", &error) && !error.empty());
    CHECK(!yh::Companions::fromJson(R"({"definitions":[{"id":"a"},{"id":"a"}]})"));
    CHECK(!yh::CompanionDefinition::fromJson(R"({"id":"a","flags":{"x":"lots"}})"));
    const auto read = yh::CompanionDefinition::fromJson(R"({"id":"a","joinAt":5,"flags":{"x":-2}})", &error);
    CHECK(read && read->joinAt == 5 && !read->leaveAt && read->flags[0].second == -2);
}

void rulesetNumbers()
{
    yh::Ruleset rules = yh::Ruleset::modern();
    CHECK(rules.companions.limit == 0 && rules.companions.partyLimit == 0);
    rules.companions = {2, 6, -50, 50};
    std::string error;
    const auto back = yh::Ruleset::fromJson(rules.toJson(), &error);
    CHECK(back && back->companions.limit == 2 && back->companions.partyLimit == 6 && back->companions.approvalMin == -50
        && back->companions.approvalMax == 50);
    nlohmann::json bad = nlohmann::json::parse(rules.toJson());
    bad["companions"]["approvalMin"] = 60;
    CHECK(!yh::Ruleset::fromJson(bad.dump()));
}

}

void companions()
{
    joiningAndCaps();
    approvalMoves();
    saving();
    rulesetNumbers();
}

}
