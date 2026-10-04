// Condition definitions <-> JSON, and loading a folder of them into a ruleset.

#include "yorehold/framework/rpg/Ruleset.h"

#include "yorehold/framework/assets/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace yh
{

namespace
{

using nlohmann::json;

constexpr std::pair<ConditionDefinition::Stacking, const char*> stackings[] = {
    {ConditionDefinition::Stacking::Refresh, "refresh"},
    {ConditionDefinition::Stacking::Longest, "longest"},
    {ConditionDefinition::Stacking::Value, "value"},
};

bool validName(std::string_view text)
{
    return !text.empty() && text.size() <= 64;
}

std::vector<std::string> names(const json& j, const char* key)
{
    const std::vector<std::string> list = j.value(key, std::vector<std::string>{});
    if (!std::all_of(list.begin(), list.end(), validName) || std::set<std::string>(list.begin(), list.end()).size() != list.size())
        throw std::invalid_argument(std::string(key) + " is a list of different names, 1 to 64 characters each");
    return list;
}

ConditionDefinition parse(const json& c)
{
    if (!c.is_object()) throw std::invalid_argument("A condition is a JSON object");
    ConditionDefinition def;
    def.id = c.at("id").get<std::string>();
    def.name = c.value("name", def.id);
    def.description = c.value("description", std::string{});
    def.advantageOnAttacks = c.value("advantageOnAttacks", false);
    def.disadvantageOnAttacks = c.value("disadvantageOnAttacks", false);
    if (!validName(def.id) || !validName(def.name)) throw std::invalid_argument("Condition ids and names are 1 to 64 characters");
    for (const json& m : c.value("modifiers", json::array()))
    {
        Modifier mod;
        mod.stat = m.at("stat").get<std::string>();
        mod.value = m.at("value").get<float>();
        const std::string op = m.value("op", std::string("add"));
        if (mod.stat.empty() || !std::isfinite(mod.value) || (op != "add" && op != "multiply" && op != "override"))
            throw std::invalid_argument("Invalid condition modifier");
        mod.op = op == "add" ? Modifier::Op::Add : op == "multiply" ? Modifier::Op::Multiply : Modifier::Op::Override;
        def.modifiers.push_back(std::move(mod));
    }
    def.flags = names(c, "flags");
    def.duration = c.value("duration", def.duration);
    if (def.duration == 0 || def.duration < -1 || def.duration > 100000)
        throw std::invalid_argument("duration is a number of rounds, or -1 for until something ends it");
    const std::string stacking = c.value("stacking", std::string("refresh"));
    const auto found = std::find_if(std::begin(stackings), std::end(stackings), [&](const auto& s) { return stacking == s.second; });
    if (found == std::end(stackings)) throw std::invalid_argument("stacking is refresh, longest or value");
    def.stacking = found->first;
    def.maxValue = c.value("maxValue", def.maxValue);
    def.perValue = c.value("perValue", def.perValue);
    def.decay = c.value("decay", def.decay);
    if (def.maxValue < 1 || def.maxValue > 1000 || def.decay < 0 || def.decay > 1000)
        throw std::invalid_argument("maxValue is 1 to 1000 and decay 0 to 1000");
    def.ends = names(c, "ends");
    for (const std::string& event : def.ends)
        if (std::none_of(std::begin(conditionEvents), std::end(conditionEvents), [&](const char* known) { return event == known; }))
            throw std::invalid_argument("Unknown event \"" + event + "\" in ends");
    if (c.contains("save"))
    {
        const json& save = c.at("save");
        def.saveAbility = save.at("ability").get<std::string>();
        def.saveDc = save.value("dc", def.saveDc);
        if (def.saveAbility.empty() || def.saveDc < -1000 || def.saveDc > 1000) throw std::invalid_argument("save needs an ability and a dc");
    }
    def.removes = names(c, "removes");
    return def;
}

json toObject(const ConditionDefinition& c)
{
    json modifiers = json::array();
    for (const Modifier& m : c.modifiers)
        modifiers.push_back({{"stat", m.stat}, {"op", m.op == Modifier::Op::Add ? "add" : m.op == Modifier::Op::Multiply ? "multiply" : "override"},
            {"value", m.value}});
    const char* stacking = "refresh";
    for (const auto& [kind, name] : stackings)
        if (kind == c.stacking) stacking = name;
    json j{{"id", c.id}, {"name", c.name}, {"description", c.description}, {"modifiers", modifiers},
        {"advantageOnAttacks", c.advantageOnAttacks}, {"disadvantageOnAttacks", c.disadvantageOnAttacks},
        {"flags", c.flags}, {"duration", c.duration}, {"stacking", stacking}, {"maxValue", c.maxValue},
        {"perValue", c.perValue}, {"decay", c.decay}, {"ends", c.ends}, {"removes", c.removes}};
    if (!c.saveAbility.empty())
        j["save"] = {{"ability", c.saveAbility}, {"dc", c.saveDc}};
    return j;
}

// What only the whole set can tell: saves use the ruleset's abilities, `removes` names conditions that exist.
void checkTogether(const Ruleset& rules)
{
    for (const ConditionDefinition& c : rules.conditions)
    {
        if (!c.saveAbility.empty() && !rules.ability(c.saveAbility))
            throw std::invalid_argument("Condition " + c.id + " saves with unknown ability \"" + c.saveAbility + "\"");
        for (const std::string& other : c.removes)
            if (!rules.condition(other))
                throw std::invalid_argument("Condition " + c.id + " removes unknown condition \"" + other + "\"");
    }
}

}

bool ConditionDefinition::hasFlag(std::string_view flag) const
{
    return std::find(flags.begin(), flags.end(), flag) != flags.end();
}

bool ConditionDefinition::endsOn(std::string_view event) const
{
    return std::find(ends.begin(), ends.end(), event) != ends.end();
}

std::string ConditionDefinition::toJson() const
{
    return toObject(*this).dump();
}

std::optional<ConditionDefinition> ConditionDefinition::fromJson(std::string_view text, std::string* error)
{
    if (error) error->clear();
    try
    {
        return parse(json::parse(text));
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

bool Ruleset::loadConditions(const FileSystem& files, std::string_view folder, std::string* error)
{
    if (error) error->clear();
    Ruleset next = *this; // all-or-nothing: work on a copy
    std::string where(folder);
    try
    {
        std::vector<std::string> paths = files.list(folder);
        std::sort(paths.begin(), paths.end()); // the same order on every machine
        for (const std::string& path : paths)
        {
            if (!path.ends_with(".json"))
                continue;
            where = path;
            const size_t slash = path.rfind('/');
            const std::string stem = path.substr(slash == std::string::npos ? 0 : slash + 1, path.size() - (slash == std::string::npos ? 0 : slash + 1) - 5);
            const std::optional<std::string> text = files.readText(path);
            if (!text) throw std::invalid_argument("can't read");
            ConditionDefinition def = parse(json::parse(*text));
            if (def.id != stem) throw std::invalid_argument("id \"" + def.id + "\" doesn't match the file name");
            const auto old = std::find_if(next.conditions.begin(), next.conditions.end(), [&](const ConditionDefinition& c) { return c.id == def.id; });
            if (old != next.conditions.end())
                *old = std::move(def);
            else
                next.conditions.push_back(std::move(def));
        }
        where = std::string(folder);
        checkTogether(next);
    }
    catch (const std::exception& e)
    {
        if (error) *error = where + ": " + e.what();
        return false;
    }
    *this = std::move(next);
    return true;
}

}
