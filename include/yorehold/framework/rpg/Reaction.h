#pragma once

#include "yorehold/framework/rpg/Action.h"

namespace yh
{

// A movement trigger that offers an action for one reaction, independent of its turn cost.
struct ReactionDefinition
{
    enum class Trigger { LeavesReach, EntersReach };
    std::string id;
    std::string name;
    Trigger trigger = Trigger::LeavesReach;
    std::string action;
    bool readied = false; // use the action the creature recorded, instead of `action`
    int order = 0;
    float promptSeconds = 2;
    std::string json;

    bool matches(float before, float after, int reach) const;
    static std::optional<ReactionDefinition> fromJson(std::string_view json, std::string* error = nullptr);
};

// One file per reaction. Replaces matching ids and changes nothing on a bad file or action reference.
bool loadReactions(const FileSystem& files, std::string_view folder, const std::vector<ActionDefinition>& actions,
    std::vector<ReactionDefinition>& reactions, std::string* error = nullptr);

}
