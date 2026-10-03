#pragma once

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

enum class QuestStatus { Hidden, Active, Completed, Failed };

struct QuestObjective
{
    std::string id;
    std::string text;
    std::vector<std::string> require; // all flags must be set to complete this objective
};

struct QuestProgress
{
    QuestStatus status = QuestStatus::Hidden;
    std::vector<bool> objectiveComplete; // same order as the authored objectives
    size_t completedObjectives() const;
    float fraction() const;
};

struct Quest
{
    std::string id;
    std::string title;
    std::string description;
    std::vector<std::string> require; // all flags must be set to reveal the quest; empty = always visible
    std::vector<std::string> fail;    // any flag fails a visible quest, even if objectives were complete
    std::vector<QuestObjective> objectives;

    QuestProgress progress(const std::set<std::string>& storyFlags) const;
};

struct QuestEntry
{
    const Quest* quest = nullptr; // borrowed from the journal, for this view
    QuestProgress progress;
};

// Progress is derived from chapter flags. There is no second mutable quest state to keep in sync
// with dialogue, world events or a restored chapter save.
struct QuestJournal
{
    std::vector<Quest> quests;

    const Quest* quest(std::string_view id) const;
    std::vector<QuestEntry> entries(const std::set<std::string>& storyFlags, bool includeHidden = false) const;
    bool validate(std::string* error = nullptr) const;
    std::string toJson() const;
    static std::optional<QuestJournal> fromJson(std::string_view json, std::string* error = nullptr);
};

}
