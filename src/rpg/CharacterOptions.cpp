// Races, backgrounds and feats <-> JSON. Strict: an unknown field or a wrong type names the field,
// so a typo in a ruleset file shows up when it loads rather than as a feat that quietly does nothing.

#include "yorehold/framework/rpg/CharacterOptions.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
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

bool isId(const json& j)
{
    if (!j.is_string()) return false;
    const std::string id = j.get<std::string>();
    return !id.empty() && id.size() <= 64
        && std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}

std::string id(const json& j)
{
    if (!j.contains("id") || !isId(j.at("id"))) fail("id", "uses a-z, 0-9, - and _");
    return j.at("id").get<std::string>();
}

std::string text(const json& j, const char* key, size_t longest)
{
    if (!j.contains(key))
        return {};
    if (!j.at(key).is_string() || j.at(key).get<std::string>().size() > longest)
        fail(key, "is text of up to " + std::to_string(longest) + " characters");
    return j.at(key).get<std::string>();
}

int number(const json& j, const std::string& path, const char* key, int fallback, int lowest, int highest)
{
    if (!j.contains(key))
        return fallback;
    const json& v = j.at(key);
    if (!v.is_number_integer() || v.get<long long>() < lowest || v.get<long long>() > highest)
        fail(path + key, "is a whole number from " + std::to_string(lowest) + " to " + std::to_string(highest));
    return v.get<int>();
}

std::vector<std::string> ids(const json& j, const std::string& path, const char* key)
{
    if (!j.contains(key))
        return {};
    const json& v = j.at(key);
    if (!v.is_array() || v.size() > 1000 || !std::all_of(v.begin(), v.end(), isId))
        fail(path + key, "is a list of ids");
    return v.get<std::vector<std::string>>();
}

std::map<std::string, int> scores(const json& j, const std::string& path, int lowest, int highest)
{
    std::map<std::string, int> result;
    if (!j.contains("abilities"))
        return result;
    if (!j.at("abilities").is_object()) fail(path + "abilities", "maps an ability to a number");
    for (const auto& [ability, value] : j.at("abilities").items())
    {
        if (!isId(json(ability)) || !value.is_number_integer() || value.get<long long>() < lowest || value.get<long long>() > highest)
            fail(path + "abilities." + ability, "is a whole number from " + std::to_string(lowest) + " to " + std::to_string(highest));
        result[ability] = value.get<int>();
    }
    return result;
}

std::set<std::string> idSet(const json& j, const char* key)
{
    const std::vector<std::string> list = ids(j, "", key);
    return {list.begin(), list.end()};
}

template <typename T, typename Parse>
std::optional<T> parse(std::string_view source, std::string* error, Parse parseObject)
{
    if (error) error->clear();
    try
    {
        const json j = json::parse(source);
        if (!j.is_object()) throw std::invalid_argument("Expected a JSON object");
        return parseObject(j);
    }
    catch (const std::exception& e)
    {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

}

std::optional<RaceDefinition> raceFromJson(std::string_view source, std::string* error)
{
    return parse<RaceDefinition>(source, error, [](const json& j) {
        onlyFields(j, "", {"id", "name", "description", "speed", "darkvision", "bonusHp", "abilities", "proficiencies", "feats"});
        RaceDefinition r;
        r.id = id(j);
        r.name = j.contains("name") ? text(j, "name", 64) : r.id;
        r.description = text(j, "description", 4000);
        r.speed = number(j, "", "speed", 0, 0, 1000);
        r.darkvision = number(j, "", "darkvision", 0, 0, 10000);
        r.bonusHp = number(j, "", "bonusHp", 0, 0, 1000);
        r.abilities = scores(j, "", -10, 10);
        r.proficiencies = idSet(j, "proficiencies");
        r.feats = ids(j, "", "feats");
        return r;
    });
}

std::optional<BackgroundDefinition> backgroundFromJson(std::string_view source, std::string* error)
{
    return parse<BackgroundDefinition>(source, error, [](const json& j) {
        onlyFields(j, "", {"id", "name", "description", "abilities", "proficiencies", "feats", "items"});
        BackgroundDefinition b;
        b.id = id(j);
        b.name = j.contains("name") ? text(j, "name", 64) : b.id;
        b.description = text(j, "description", 4000);
        b.abilities = scores(j, "", -10, 10);
        b.proficiencies = idSet(j, "proficiencies");
        b.feats = ids(j, "", "feats");
        b.items = ids(j, "", "items");
        return b;
    });
}

std::optional<FeatDefinition> featFromJson(std::string_view source, std::string* error)
{
    return parse<FeatDefinition>(source, error, [](const json& j) {
        onlyFields(j, "", {"id", "name", "description", "kind", "repeatable", "requires", "modifiers", "proficiencies", "ranks", "resources"});
        FeatDefinition f;
        f.id = id(j);
        f.name = j.contains("name") ? text(j, "name", 64) : f.id;
        f.description = text(j, "description", 4000);
        if (j.contains("kind"))
        {
            f.kind = text(j, "kind", 64);
            if (std::none_of(std::begin(FeatDefinition::kinds), std::end(FeatDefinition::kinds), [&](const char* k) { return f.kind == k; }))
                fail("kind", "is \"class\", \"skill\", \"general\" or \"race\"");
        }
        if (j.contains("repeatable"))
        {
            if (!j.at("repeatable").is_boolean()) fail("repeatable", "is true or false");
            f.repeatable = j.at("repeatable").get<bool>();
        }

        if (j.contains("requires"))
        {
            const json& r = j.at("requires");
            if (!r.is_object()) fail("requires", "is an object");
            onlyFields(r, "requires.", {"level", "races", "classes", "abilities", "proficiencies"});
            f.needs.level = number(r, "requires.", "level", 1, 1, 1000);
            f.needs.races = ids(r, "requires.", "races");
            f.needs.classes = ids(r, "requires.", "classes");
            f.needs.abilities = scores(r, "requires.", 1, 30);
            f.needs.proficiencies = ids(r, "requires.", "proficiencies");
        }

        if (j.contains("modifiers"))
        {
            if (!j.at("modifiers").is_array() || j.at("modifiers").size() > 100) fail("modifiers", "is a list");
            for (size_t i = 0; i < j.at("modifiers").size(); i++)
            {
                const json& m = j.at("modifiers").at(i);
                const std::string path = "modifiers[" + std::to_string(i) + "].";
                if (!m.is_object()) fail(path.substr(0, path.size() - 1), "is an object with a stat and a value");
                onlyFields(m, path, {"stat", "op", "value"});
                Modifier modifier;
                // Stats are camelCase ("maxHp"), so not held to the id rules.
                if (!m.contains("stat") || !m.at("stat").is_string() || m.at("stat").get<std::string>().empty()
                    || m.at("stat").get<std::string>().size() > 64)
                    fail(path + "stat", "is a stat name");
                modifier.stat = m.at("stat").get<std::string>();
                const std::string op = m.contains("op") && m.at("op").is_string() ? m.at("op").get<std::string>() : "add";
                if (op == "multiply") modifier.op = Modifier::Op::Multiply;
                else if (op == "override") modifier.op = Modifier::Op::Override;
                else if (op != "add" || (m.contains("op") && !m.at("op").is_string())) fail(path + "op", "is \"add\", \"multiply\" or \"override\"");
                if (!m.contains("value") || !m.at("value").is_number() || !std::isfinite(m.at("value").get<double>()))
                    fail(path + "value", "is a number");
                modifier.value = m.at("value").get<float>();
                f.modifiers.push_back(std::move(modifier));
            }
        }
        f.proficiencies = idSet(j, "proficiencies");
        if (j.contains("ranks"))
        {
            if (!j.at("ranks").is_object()) fail("ranks", "maps a skill, ability, \"weapons\", \"armor\" or \"dc\" to a rank");
            for (const auto& [target, rank] : j.at("ranks").items())
            {
                if (!isId(json(target)) || !isId(rank)) fail("ranks." + target, "is a rank id");
                f.ranks[target] = rank.get<std::string>();
            }
        }
        if (j.contains("resources"))
        {
            if (!j.at("resources").is_object()) fail("resources", "maps a resource to the maximum it adds");
            for (const auto& [resource, amount] : j.at("resources").items())
            {
                if (!isId(json(resource)) || !amount.is_number_integer() || amount.get<long long>() < 1 || amount.get<long long>() > 1000)
                    fail("resources." + resource, "is a whole number from 1 to 1000");
                f.resources[resource] = {amount.get<int>(), amount.get<int>()};
            }
        }
        return f;
    });
}

}
