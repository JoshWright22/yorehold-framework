#include "Checks.h"
#include "DialogueTests.h"

#include <yorehold/framework/rpg/Character.h>
#include <yorehold/framework/rpg/Dialogue.h>

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace regression
{
namespace
{

const char* story = R"({
    "id":"gate", "start":"hello", "nodes":[
        {"id":"hello", "speaker":"Mara", "text":"The keep is closed.", "set":["met_guard"], "choices":[
            {"id":"ask", "text":"What happened?", "next":"hello", "set":["heard_rumor"], "forbid":["heard_rumor"]},
            {"id":"known", "text":"About those goblins...", "next":"open", "require":["heard_rumor"]},
            {"id":"convince", "text":"Let us help.", "set":["tried"],
                "check":{"skill":"persuasion", "difficulty":12, "success":"open", "failure":"refused"}},
            {"id":"leave", "text":"Farewell.", "next":""}
        ]},
        {"id":"open", "text":"The gate opens.", "set":["gate_open"], "clear":["tried"]},
        {"id":"refused", "text":"Come back with proof.", "choices":[{"id":"back", "text":"Try again", "next":"hello"}]}
    ]
})";

void branchesAndFlags()
{
    std::string error;
    const auto authored = yh::Dialogue::fromJson(story, &error);
    CHECK(authored && error.empty());
    if (!authored) return;
    const auto roundTrip = yh::Dialogue::fromJson(authored->toJson());
    CHECK(roundTrip && roundTrip->toJson() == authored->toJson());

    yh::DialogueSession session(*authored);
    CHECK(session.current()->speaker == "Mara" && session.flags().contains("met_guard"));
    CHECK(session.choices().size() == 3 && !session.finished());
    CHECK(!session.choose("known") && !session.choose("missing") && !session.choose("convince"));
    CHECK(session.history().empty() && !session.flags().contains("tried"));
    const auto ask = session.choose("ask");
    CHECK(ask && ask->from == "hello" && ask->to == "hello" && !ask->roll);
    CHECK(session.flags().contains("heard_rumor") && session.choices().size() == 3);
    CHECK(!session.choose("ask"));
    CHECK(session.choose("known") && session.current()->id == "open" && session.finished());
    CHECK(session.flags().contains("gate_open") && session.choices().empty() && !session.choose("leave"));
    session.close();
    CHECK(!session.current() && session.finished());

    yh::DialogueSession leave(*authored);
    CHECK(leave.choose("leave") && !leave.current() && leave.finished() && leave.history().size() == 1);
    yh::DialogueSession returning(*authored, {"heard_rumor"});
    CHECK(returning.choices()[0]->id == "known");
}

void checksAndReplay()
{
    const auto dialogue = *yh::Dialogue::fromJson(story);
    for (int total : {11, 12})
    {
        yh::DialogueSession session(dialogue);
        const auto result = session.choose("convince", [total](std::string_view skill) {
            CHECK(skill == "persuasion");
            return yh::RollResult{"1d20", total, {}, 0};
        });
        CHECK(result && result->roll && result->roll->total == total && result->passed == (total == 12));
        CHECK(session.current()->id == (total == 12 ? "open" : "refused"));
        CHECK(session.flags().contains("tried") == (total == 11));
    }
    yh::DialogueSession interrupted(dialogue);
    try { interrupted.choose("convince", [](std::string_view) -> yh::RollResult { throw std::runtime_error("Roll unavailable"); }); }
    catch (const std::runtime_error&) {}
    CHECK(interrupted.current()->id == "hello" && interrupted.history().empty() && !interrupted.flags().contains("tried"));

    const auto rules = yh::Ruleset::modern();
    yh::Character hero;
    hero.stats.setBase("cha", 16);
    yh::Random randomA(73), randomB(73);
    yh::DialogueSession a(dialogue), b(dialogue);
    const auto first = a.choose("convince", [&](std::string_view skill) { return hero.rollCheck(rules, skill, yh::Advantage::None, randomA); });
    const auto replay = b.choose("convince", [&](std::string_view skill) { return hero.rollCheck(rules, skill, yh::Advantage::None, randomB); });
    CHECK(first && replay && first->roll->describe() == replay->roll->describe() && first->to == replay->to);
}

void malformedData()
{
    using Json = nlohmann::json;
    const auto original = Json::parse(story);
    const auto reject = [&](Json data) {
        std::string error;
        CHECK(!yh::Dialogue::fromJson(data.dump(), &error) && !error.empty());
    };
    auto data = original; data["start"] = "absent"; reject(data);
    data = original; data["nodes"][1]["id"] = "hello"; reject(data);
    data = original; data["nodes"][0]["choices"][0]["next"] = "absent"; reject(data);
    data = original; data["nodes"][0]["choices"][2]["check"]["success"] = "absent"; reject(data);
    data = original; data["nodes"][0]["choices"][2]["next"] = "open"; reject(data);
    data = original; data["nodes"][0]["choices"][2]["check"]["difficulty"] = -1; reject(data);
    data = original; data["nodes"][0]["choices"][0]["set"] = {""}; reject(data);
    data = original; data["nodes"][0]["choices"][0]["require"] = {"heard_rumor"}; reject(data);
    data = original; data["nodes"][0]["clear"] = {"met_guard"}; reject(data);
    data = original; data["nodes"] = Json::object(); reject(data);
    data = original; data["nodes"][0]["choices"] = "bad"; reject(data);
    data = original; data["nodes"][0]["choices"][0]["require"] = 5; reject(data);
    data = original; data["nodes"][0]["choices"][1]["id"] = "ask"; reject(data);
    data = original; data["nodes"][0]["choices"][2]["check"]["difficulty"] = "hard"; reject(data);
    data = original; data["nodes"][0]["choices"][2]["check"]["difficulty"] = 12.5; reject(data);
    data = original; data["nodes"][0]["choices"][2]["check"]["difficulty"] = uint64_t(4294967308); reject(data);
    CHECK(!yh::Dialogue::fromJson("{") && !yh::Dialogue::fromJson("null"));
    CHECK(!yh::Dialogue::fromJson(std::string(4 * 1024 * 1024 + 1, ' ')));
    bool refused = false;
    try { yh::DialogueSession invalid(yh::Dialogue{}); }
    catch (const std::invalid_argument&) { refused = true; }
    CHECK(refused);
}

}

void dialogues()
{
    branchesAndFlags();
    checksAndReplay();
    malformedData();
}

}
