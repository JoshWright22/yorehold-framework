#include "yorehold/framework/rpg/Reaction.h"

#include "yorehold/framework/assets/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace yh
{

bool ReactionDefinition::matches(float before, float after, int reach) const
{
    const float limit = static_cast<float>(reach) + 0.01f;
    return trigger == Trigger::LeavesReach ? before <= limit && after > limit : before > limit && after <= limit;
}

std::optional<ReactionDefinition> ReactionDefinition::fromJson(std::string_view source, std::string* error)
{
    if (error) error->clear();
    try
    {
        const nlohmann::json j = nlohmann::json::parse(source);
        if (!j.is_object()) throw std::invalid_argument("reaction: is an object");
        for (auto it = j.begin(); it != j.end(); ++it)
            if (it.key() != "id" && it.key() != "name" && it.key() != "trigger" && it.key() != "action"
                && it.key() != "readied" && it.key() != "order" && it.key() != "promptSeconds")
                throw std::invalid_argument(it.key() + ": unknown field");
        auto text = [&](const char* key, const std::string& fallback) {
            if (!j.contains(key)) return fallback;
            if (!j.at(key).is_string() || j.at(key).get<std::string>().empty() || j.at(key).get<std::string>().size() > 64)
                throw std::invalid_argument(std::string(key) + ": is a name of 1 to 64 characters");
            return j.at(key).get<std::string>();
        };
        ReactionDefinition def;
        def.id = text("id", "");
        if (def.id.empty()) throw std::invalid_argument("id: is needed");
        def.name = text("name", def.id);
        const std::string trigger = text("trigger", "");
        if (trigger != "leavesReach" && trigger != "entersReach")
            throw std::invalid_argument("trigger: is leavesReach or entersReach");
        def.trigger = trigger == "leavesReach" ? Trigger::LeavesReach : Trigger::EntersReach;
        if (j.contains("readied") && !j.at("readied").is_boolean()) throw std::invalid_argument("readied: is true or false");
        def.readied = j.value("readied", false);
        def.action = text("action", "");
        if (def.readied ? !def.action.empty() : def.action.empty()) throw std::invalid_argument("action: name an action, or set readied, but not both");
        if (j.contains("order"))
        {
            if (!j.at("order").is_number_integer() || j.at("order").get<long long>() < -100000 || j.at("order").get<long long>() > 100000)
                throw std::invalid_argument("order: is a whole number from -100000 to 100000");
            def.order = j.at("order").get<int>();
        }
        if (j.contains("promptSeconds"))
        {
            if (!j.at("promptSeconds").is_number() || !std::isfinite(j.at("promptSeconds").get<double>())
                || j.at("promptSeconds").get<double>() < 0.1 || j.at("promptSeconds").get<double>() > 30)
                throw std::invalid_argument("promptSeconds: is from 0.1 to 30");
            def.promptSeconds = j.at("promptSeconds").get<float>();
        }
        def.json = j.dump();
        return def;
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

bool loadReactions(const FileSystem& files, std::string_view folder, const std::vector<ActionDefinition>& actions,
    std::vector<ReactionDefinition>& reactions, std::string* error)
{
    if (error) error->clear();
    std::vector<ReactionDefinition> next = reactions;
    std::vector<std::string> paths = files.list(folder);
    std::sort(paths.begin(), paths.end());
    for (const std::string& path : paths)
    {
        if (!path.ends_with(".json")) continue;
        std::string problem;
        const std::optional<std::string> text = files.readText(path);
        const std::optional<ReactionDefinition> def = text ? ReactionDefinition::fromJson(*text, &problem) : std::nullopt;
        if (def)
        {
            const size_t start = path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1;
            if (def->id != path.substr(start, path.size() - start - 5)) problem = "id: must match the file name";
            if (!def->readied)
            {
                const ActionDefinition* action = findAction(actions, def->action);
                if (!action || action->endsTurn || !action->readies.empty()) problem = "action: must name an action that neither readies another nor ends the turn";
            }
        }
        if (!def || !problem.empty())
        {
            if (error) *error = path + ": " + (problem.empty() ? "can't read" : problem);
            return false;
        }
        const auto old = std::find_if(next.begin(), next.end(), [&](const ReactionDefinition& r) { return r.id == def->id; });
        if (old == next.end()) next.push_back(*def);
        else *old = *def;
    }
    std::sort(next.begin(), next.end(), [](const ReactionDefinition& a, const ReactionDefinition& b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    reactions = std::move(next);
    return true;
}

}
