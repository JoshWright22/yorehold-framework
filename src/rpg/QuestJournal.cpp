#include "yorehold/framework/rpg/QuestJournal.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace yh
{
namespace
{

using Json = nlohmann::json;

bool fail(std::string* error, std::string message)
{
    if (error) *error = std::move(message);
    return false;
}

bool validFlags(const std::vector<std::string>& flags)
{
    std::set<std::string> seen;
    return std::all_of(flags.begin(), flags.end(), [&](const auto& flag) { return !flag.empty() && seen.insert(flag).second; });
}

bool allSet(const std::vector<std::string>& required, const std::set<std::string>& flags)
{
    return std::all_of(required.begin(), required.end(), [&](const auto& flag) { return flags.contains(flag); });
}

}

size_t QuestProgress::completedObjectives() const
{
    return static_cast<size_t>(std::count(objectiveComplete.begin(), objectiveComplete.end(), true));
}

float QuestProgress::fraction() const
{
    return objectiveComplete.empty() ? 0 : static_cast<float>(completedObjectives()) / static_cast<float>(objectiveComplete.size());
}

QuestProgress Quest::progress(const std::set<std::string>& storyFlags) const
{
    QuestProgress result;
    for (const auto& objective : objectives) result.objectiveComplete.push_back(allSet(objective.require, storyFlags));
    if (!allSet(require, storyFlags)) return result;

    const bool failed = std::any_of(fail.begin(), fail.end(), [&](const auto& flag) { return storyFlags.contains(flag); });
    const bool completed = !objectives.empty() && result.completedObjectives() == objectives.size();
    result.status = failed ? QuestStatus::Failed : completed ? QuestStatus::Completed : QuestStatus::Active;
    return result;
}

const Quest* QuestJournal::quest(std::string_view id) const
{
    const auto found = std::find_if(quests.begin(), quests.end(), [&](const auto& quest) { return quest.id == id; });
    return found == quests.end() ? nullptr : &*found;
}

std::vector<QuestEntry> QuestJournal::entries(const std::set<std::string>& storyFlags, bool includeHidden) const
{
    std::vector<QuestEntry> result;
    for (const auto& quest : quests)
    {
        auto progress = quest.progress(storyFlags);
        if (includeHidden || progress.status != QuestStatus::Hidden) result.push_back({&quest, std::move(progress)});
    }
    return result;
}

bool QuestJournal::validate(std::string* error) const
{
    if (error) error->clear();
    if (quests.size() > 512) return fail(error, "A quest journal can contain at most 512 quests");
    std::set<std::string> ids;
    for (const auto& quest : quests)
    {
        if (quest.id.empty() || quest.title.empty() || !ids.insert(quest.id).second)
            return fail(error, "Quest needs a unique id and a title: " + quest.id);
        if (!validFlags(quest.require) || !validFlags(quest.fail)) return fail(error, "Invalid flags on quest: " + quest.id);
        if (quest.objectives.empty() || quest.objectives.size() > 128) return fail(error, "Quest needs 1 to 128 objectives: " + quest.id);
        std::set<std::string> objectiveIds;
        for (const auto& objective : quest.objectives)
        {
            if (objective.id.empty() || objective.text.empty() || !objectiveIds.insert(objective.id).second)
                return fail(error, "Empty or duplicate objective: " + quest.id + "/" + objective.id);
            if (objective.require.empty() || !validFlags(objective.require))
                return fail(error, "Objective needs distinct, non-empty completion flags: " + quest.id + "/" + objective.id);
        }
    }
    return true;
}

std::string QuestJournal::toJson() const
{
    Json json{{"quests", Json::array()}};
    for (const auto& quest : quests)
    {
        Json entry{{"id", quest.id}, {"title", quest.title}, {"description", quest.description}, {"objectives", Json::array()}};
        if (!quest.require.empty()) entry["require"] = quest.require;
        if (!quest.fail.empty()) entry["fail"] = quest.fail;
        for (const auto& objective : quest.objectives)
            entry["objectives"].push_back({{"id", objective.id}, {"text", objective.text}, {"require", objective.require}});
        json["quests"].push_back(std::move(entry));
    }
    return json.dump(2);
}

std::optional<QuestJournal> QuestJournal::fromJson(std::string_view text, std::string* error)
{
    if (error) error->clear();
    if (text.size() > 4 * 1024 * 1024) { fail(error, "Quest document is too large"); return std::nullopt; }
    try
    {
        const auto json = Json::parse(text);
        const auto& quests = json.at("quests");
        if (!quests.is_array() || quests.size() > 512) throw std::invalid_argument("Quests must be an array of at most 512 entries");
        QuestJournal journal;
        for (const auto& entry : quests)
        {
            if (!entry.is_object()) throw std::invalid_argument("Quest must be an object");
            Quest quest;
            quest.id = entry.at("id").get<std::string>();
            quest.title = entry.at("title").get<std::string>();
            quest.description = entry.value("description", std::string{});
            quest.require = entry.value("require", std::vector<std::string>{});
            quest.fail = entry.value("fail", std::vector<std::string>{});
            const auto& objectives = entry.at("objectives");
            if (!objectives.is_array() || objectives.empty() || objectives.size() > 128)
                throw std::invalid_argument("Objectives must be an array of 1 to 128 entries");
            for (const auto& objective : objectives)
            {
                if (!objective.is_object()) throw std::invalid_argument("Objective must be an object");
                quest.objectives.push_back({objective.at("id").get<std::string>(), objective.at("text").get<std::string>(),
                    objective.at("require").get<std::vector<std::string>>()});
            }
            journal.quests.push_back(std::move(quest));
        }
        if (!journal.validate(error)) return std::nullopt;
        return journal;
    }
    catch (const std::exception& exception)
    {
        fail(error, exception.what());
        return std::nullopt;
    }
}

}
