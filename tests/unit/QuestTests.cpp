#include "Checks.h"
#include "QuestTests.h"

#include <yorehold/framework/rpg/Dialogue.h>
#include <yorehold/framework/rpg/QuestJournal.h>

#include <nlohmann/json.hpp>

namespace regression
{
namespace
{

const char* authored = R"({"quests":[{
    "id":"rescue", "title":"Bring them home", "description":"Free the villagers.",
    "require":["accepted"], "fail":["abandoned","prisoners_dead"], "objectives":[
        {"id":"prisoners","text":"Escort the prisoners.","require":["freed","escorted"]},
        {"id":"chief","text":"Defeat the chief.","require":["chief_dead"]}
    ]
}]})";

void storyProgress()
{
    std::string error;
    const auto journal = yh::QuestJournal::fromJson(authored, &error);
    CHECK(journal && error.empty());
    if (!journal) return;
    const auto* quest = journal->quest("rescue");
    CHECK(quest && !journal->quest("missing"));
    std::set<std::string> flags;
    CHECK(journal->entries(flags).empty() && journal->entries(flags, true).size() == 1);
    CHECK(quest->progress(flags).status == yh::QuestStatus::Hidden);
    flags.insert("accepted");
    auto progress = quest->progress(flags);
    CHECK(progress.status == yh::QuestStatus::Active && progress.fraction() == 0 && progress.objectiveComplete.size() == 2);
    flags.insert("freed");
    CHECK(quest->progress(flags).completedObjectives() == 0); // every required flag must be set
    flags.insert("escorted");
    progress = quest->progress(flags);
    CHECK(progress.completedObjectives() == 1 && near(progress.fraction(), 0.5f) && !progress.objectiveComplete[1]);
    flags.insert("chief_dead");
    CHECK(quest->progress(flags).status == yh::QuestStatus::Completed && quest->progress(flags).fraction() == 1);
    flags.insert("abandoned");
    CHECK(quest->progress(flags).status == yh::QuestStatus::Failed);
    flags.erase("abandoned");
    flags.insert("prisoners_dead");
    CHECK(quest->progress(flags).status == yh::QuestStatus::Failed);
    flags.erase("accepted");
    CHECK(quest->progress(flags).status == yh::QuestStatus::Hidden); // undiscovered quests stay hidden
    const auto roundTrip = yh::QuestJournal::fromJson(journal->toJson());
    CHECK(roundTrip && roundTrip->toJson() == journal->toJson());
    yh::Quest always = *quest;
    always.require.clear();
    CHECK(always.progress({}).status == yh::QuestStatus::Active);
    CHECK(yh::QuestProgress{}.fraction() == 0);
}

void conversationIntegration()
{
    const auto journal = *yh::QuestJournal::fromJson(authored);
    const auto dialogue = *yh::Dialogue::fromJson(R"({"id":"captain","start":"offer","nodes":[
        {"id":"offer","choices":[{"id":"accept","text":"We will help.","next":"","set":["accepted"]}]}
    ]})");
    yh::DialogueSession conversation(dialogue);
    const auto before = conversation.snapshot();
    CHECK(journal.entries(conversation.flags()).empty());
    CHECK(conversation.choose("accept") && journal.entries(conversation.flags()).size() == 1);
    CHECK(conversation.restore(before) && journal.entries(conversation.flags()).empty());
}

void badDefinitions()
{
    using Json = nlohmann::json;
    const auto original = Json::parse(authored);
    const auto reject = [&](Json json) {
        std::string error;
        CHECK(!yh::QuestJournal::fromJson(json.dump(), &error) && !error.empty());
    };
    auto json = original; json["quests"].push_back(json["quests"][0]); reject(json);
    json = original; json["quests"][0]["id"] = ""; reject(json);
    json = original; json["quests"][0]["title"] = ""; reject(json);
    json = original; json["quests"][0]["require"] = {"accepted","accepted"}; reject(json);
    json = original; json["quests"][0]["fail"] = {""}; reject(json);
    json = original; json["quests"][0]["objectives"][1]["id"] = "prisoners"; reject(json);
    json = original; json["quests"][0]["objectives"][0]["require"] = Json::array(); reject(json);
    json = original; json["quests"][0]["objectives"][0]["text"] = 42; reject(json);
    json = original; json["quests"][0]["objectives"] = Json::array(); reject(json);
    json = original; json["quests"] = Json::object(); reject(json);
    CHECK(!yh::QuestJournal::fromJson("null") && !yh::QuestJournal::fromJson("{"));
    CHECK(!yh::QuestJournal::fromJson(std::string(4 * 1024 * 1024 + 1, ' ')));
    const auto empty = yh::QuestJournal::fromJson(R"({"quests":[]})");
    CHECK(empty && empty->entries({}).empty());
}

}

void quests()
{
    storyProgress();
    conversationIntegration();
    badDefinitions();
}

}
