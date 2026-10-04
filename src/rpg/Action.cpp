// Action definitions from JSON, and loading a folder of them.

#include "yorehold/framework/rpg/Action.h"

#include "yorehold/framework/assets/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <initializer_list>
#include <stdexcept>

namespace yh
{

namespace
{

using nlohmann::json;

[[noreturn]] void fail(const std::string& field, const std::string& what)
{
    throw std::invalid_argument(field + ": " + what);
}

void onlyFields(const json& j, const std::string& path, std::initializer_list<const char*> known)
{
    for (auto it = j.begin(); it != j.end(); ++it)
        if (std::none_of(known.begin(), known.end(), [&](const char* field) { return it.key() == field; }))
            fail(path + it.key(), "unknown field");
}

std::string text(const json& j, const char* key, const std::string& fallback, size_t longest)
{
    if (!j.contains(key))
        return fallback;
    if (!j.at(key).is_string() || j.at(key).get<std::string>().size() > longest)
        fail(key, "is text of up to " + std::to_string(longest) + " characters");
    return j.at(key).get<std::string>();
}

std::vector<std::string> names(const json& j, const std::string& path, const char* key)
{
    if (!j.contains(key))
        return {};
    const json& list = j.at(key);
    const bool fine = list.is_array() && std::all_of(list.begin(), list.end(), [](const json& name) {
        return name.is_string() && !name.get<std::string>().empty() && name.get<std::string>().size() <= 64;
    });
    if (!fine) fail(path + key, "is a list of names, 1 to 64 characters each");
    return list.get<std::vector<std::string>>();
}

ActionDefinition parse(const json& j)
{
    if (!j.is_object()) fail("action", "is a JSON object");
    onlyFields(j, "", {"id", "name", "description", "order", "cost", "endsTurn", "general", "requires", "target", "log", "save", "effects"});
    ActionDefinition def;
    def.id = text(j, "id", "", 64);
    if (def.id.empty()) fail("id", "is needed, 1 to 64 characters");
    def.name = text(j, "name", def.id, 64);
    if (def.name.empty()) fail("name", "can't be empty");
    def.description = text(j, "description", "", 2000);
    def.log = text(j, "log", "", 200);
    if (j.contains("order"))
    {
        if (!j.at("order").is_number_integer() || j.at("order").get<long long>() < -100000 || j.at("order").get<long long>() > 100000)
            fail("order", "is a whole number");
        def.order = j.at("order").get<int>();
    }
    if (j.contains("cost"))
    {
        const json& cost = j.at("cost");
        if (cost == "hands")
            def.costsHands = true;
        else if (cost.is_number_integer() && cost.get<long long>() >= 0 && cost.get<long long>() <= 10)
            def.cost = cost.get<int>();
        else
            fail("cost", "is a number of actions from 0 to 10, or \"hands\"");
    }
    for (const char* key : {"endsTurn", "general"})
        if (j.contains(key) && !j.at(key).is_boolean())
            fail(key, "is true or false");
    def.endsTurn = j.value("endsTurn", false);
    def.general = j.value("general", true);

    if (j.contains("requires"))
    {
        const json& requires_ = j.at("requires");
        if (!requires_.is_object()) fail("requires", "is an object");
        onlyFields(requires_, "requires.", {"flags", "without", "resources"});
        def.needsFlags = names(requires_, "requires.", "flags");
        def.barredBy = names(requires_, "requires.", "without");
        if (requires_.contains("resources"))
        {
            const json& resources = requires_.at("resources");
            if (!resources.is_object()) fail("requires.resources", "maps resource names to amounts");
            for (auto it = resources.begin(); it != resources.end(); ++it)
            {
                if (it.key().empty() || !it.value().is_number_integer() || it.value().get<long long>() < 1 || it.value().get<long long>() > 100000)
                    fail("requires.resources." + it.key(), "is an amount of 1 or more");
                def.needsResources.emplace_back(it.key(), it.value().get<int>());
            }
        }
    }

    if (j.contains("target"))
    {
        const json& target = j.at("target");
        if (!target.is_object()) fail("target", "is an object with a kind");
        onlyFields(target, "target.", {"kind", "side", "range"});
        const std::string kind = target.value("kind", json("self")).is_string() ? target.value("kind", std::string("self")) : std::string();
        if (kind != "self" && kind != "creature") fail("target.kind", "is \"self\" or \"creature\"");
        def.target = kind == "self" ? ActionDefinition::Target::Self : ActionDefinition::Target::Creature;
        const std::string side = target.value("side", json("enemy")).is_string() ? target.value("side", std::string("enemy")) : std::string();
        if (side != "enemy" && side != "ally" && side != "any") fail("target.side", "is \"enemy\", \"ally\" or \"any\"");
        def.side = side == "enemy" ? ActionDefinition::Side::Enemy : side == "ally" ? ActionDefinition::Side::Ally : ActionDefinition::Side::Any;
        if (target.contains("range"))
        {
            if (!target.at("range").is_number_integer() || target.at("range").get<long long>() < 1 || target.at("range").get<long long>() > 1000)
                fail("target.range", "is a number of squares from 1 to 1000");
            def.range = target.at("range").get<int>();
        }
        if (def.target == ActionDefinition::Target::Self && (target.contains("side") || target.contains("range")))
            fail("target", "side and range are for a creature target");
    }

    json effect = json::object();
    effect["effects"] = j.value("effects", json::array());
    if (j.contains("save"))
        effect["save"] = j.at("save");
    std::string problem;
    std::optional<Effect> parsed = Effect::fromJson(effect.dump(), &problem);
    if (!parsed) throw std::invalid_argument(problem);
    def.effect = std::move(*parsed);
    def.json = j.dump();
    return def;
}

void sortActions(std::vector<ActionDefinition>& actions)
{
    std::stable_sort(actions.begin(), actions.end(), [](const ActionDefinition& a, const ActionDefinition& b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
}

}

int ActionDefinition::costFor(const Character& character, const Ruleset& rules) const
{
    if (!costsHands)
        return cost;
    const Item* held = character.weapon();
    return held ? std::clamp(held->hands, 1, std::max(1, rules.actionsPerTurn)) : 1;
}

bool ActionDefinition::meets(const Character& character, const Ruleset& rules, std::string* why) const
{
    if (why) why->clear();
    for (const std::string& flag : needsFlags)
        if (!character.hasFlag(rules, flag))
        {
            if (why) *why = "needs " + flag;
            return false;
        }
    for (const std::string& flag : barredBy)
        if (character.hasFlag(rules, flag))
        {
            if (why) *why = "not while " + flag;
            return false;
        }
    for (const auto& [resource, amount] : needsResources)
    {
        const auto found = character.resources.find(resource);
        if (found == character.resources.end() || found->second.current < amount)
        {
            if (why) *why = "needs " + std::to_string(amount) + " " + resource;
            return false;
        }
    }
    return true;
}

std::optional<ActionDefinition> ActionDefinition::fromJson(std::string_view source, std::string* error)
{
    if (error) error->clear();
    try
    {
        return parse(json::parse(source));
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

std::vector<ActionDefinition> basicActions(const Ruleset& rules)
{
    const std::string strike = std::string(R"({"id":"strike","name":"Strike","order":10,"cost":)") + (rules.strikeCostsHands ? "\"hands\"" : "1")
        + R"(,"target":{"kind":"creature","side":"enemy","range":1},
            "effects":[{"do":"roll","kind":"attack","steps":[{"do":"damage","dice":"weapon","when":"hit","minimum":1}]}]})";
    const char* stride = R"({"id":"stride","name":"Dash","order":20,"cost":1,"log":"{name} dashes",
        "effects":[{"do":"resource","id":"movement","op":"restore","amount":"speed","target":"self"}]})";
    const char* endTurn = R"({"id":"end-turn","name":"End turn","order":1000,"cost":0,"endsTurn":true})";
    std::vector<ActionDefinition> actions;
    for (const std::string& source : {strike, std::string(stride), std::string(endTurn)})
        actions.push_back(*ActionDefinition::fromJson(source));
    return actions;
}

bool loadActions(const FileSystem& files, std::string_view folder, const Ruleset& rules, std::vector<ActionDefinition>& actions, std::string* error)
{
    if (error) error->clear();
    std::vector<ActionDefinition> next = actions; // all-or-nothing: work on a copy
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
            const size_t start = path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1;
            const std::string stem = path.substr(start, path.size() - start - 5);
            const std::optional<std::string> text = files.readText(path);
            if (!text) throw std::invalid_argument("can't read");
            ActionDefinition def = parse(json::parse(*text));
            if (def.id != stem) throw std::invalid_argument("id \"" + def.id + "\" doesn't match the file name");
            std::string problem;
            if (!def.effect.check(rules, &problem)) throw std::invalid_argument(problem);
            const auto old = std::find_if(next.begin(), next.end(), [&](const ActionDefinition& a) { return a.id == def.id; });
            if (old != next.end())
                *old = std::move(def);
            else
                next.push_back(std::move(def));
        }
    }
    catch (const std::exception& e)
    {
        if (error) *error = where + ": " + e.what();
        return false;
    }
    sortActions(next);
    actions = std::move(next);
    return true;
}

const ActionDefinition* findAction(const std::vector<ActionDefinition>& actions, std::string_view id)
{
    const auto found = std::find_if(actions.begin(), actions.end(), [&](const ActionDefinition& a) { return a.id == id; });
    return found == actions.end() ? nullptr : &*found;
}

}
