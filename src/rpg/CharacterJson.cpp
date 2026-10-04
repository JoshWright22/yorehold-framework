// Character <-> JSON. Kept apart from Character.cpp so only this file pays for including nlohmann/json.

#include "yorehold/framework/rpg/Character.h"
#include "yorehold/framework/rpg/Action.h"
#include "yorehold/framework/rpg/Compendium.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace yh
{

namespace
{

using nlohmann::json;

constexpr int sheetVersion = 1;

const char* opName(Modifier::Op op)
{
    switch (op)
    {
    case Modifier::Op::Multiply: return "multiply";
    case Modifier::Op::Override: return "override";
    case Modifier::Op::Add: break;
    }
    return "add";
}

Modifier::Op opFromName(const std::string& name)
{
    if (name == "multiply")
        return Modifier::Op::Multiply;
    if (name == "override")
        return Modifier::Op::Override;
    return Modifier::Op::Add;
}

json modifierJson(const Modifier& m)
{
    return {{"stat", m.stat}, {"op", opName(m.op)}, {"value", m.value}, {"source", m.source}};
}

Modifier modifierFrom(const json& j)
{
    return {j.value("stat", ""), opFromName(j.value("op", "add")), j.value("value", 0.0f), j.value("source", "")};
}

}

std::string Character::toJson() const
{
    json j;
    j["version"] = sheetVersion;
    j["name"] = name;
    j["ancestry"] = ancestry;
    j["class"] = characterClass;
    j["level"] = level;
    j["xp"] = xp;
    j["hitDie"] = hitDie;
    j["hp"] = hp;
    j["tempHp"] = tempHp;
    j["notes"] = notes;
    j["proficiencies"] = proficiencies;
    j["proficiencyRanks"] = proficiencyRanks;
    j["dcAbility"] = dcAbility;
    j["death"] = {{"saves", death.saves}, {"successes", death.successes}, {"failures", death.failures}, {"stable", death.stable}, {"dead", death.dead}};

    json stats_ = json::object();
    for (const auto& [stat, value] : stats.bases())
        stats_[stat] = value;
    j["stats"] = stats_;

    json modifiers = json::array();
    for (const Modifier& m : stats.modifiers())
        modifiers.push_back(modifierJson(m));
    j["modifiers"] = modifiers;

    json resources_ = json::object();
    for (const auto& [id, r] : resources)
        resources_[id] = {{"current", r.current}, {"max", r.max}};
    j["resources"] = resources_;

    json items = json::array();
    for (const Item& item : inventory)
    {
        json itemModifiers = json::array();
        for (const Modifier& m : item.modifiers)
            itemModifiers.push_back(modifierJson(m));
        items.push_back({
            {"id", item.id}, {"name", item.name}, {"slot", item.slot}, {"damage", item.damage},
            {"attackAbility", item.attackAbility}, {"hands", item.hands}, {"weight", item.weight}, {"value", item.value},
            {"quantity", item.quantity}, {"magic", item.magic}, {"equipped", item.equipped}, {"modifiers", itemModifiers},
        });
        if (item.use) items.back()["use"] = json::parse(item.use->json);
    }
    j["inventory"] = items;
    j["coins"] = coins;

    json conditions_ = json::array();
    for (const ActiveCondition& c : conditions)
        conditions_.push_back({{"id", c.id}, {"roundsLeft", c.roundsLeft}, {"value", c.value}});
    j["conditions"] = conditions_;

    return j.dump(2);
}

std::optional<Character> Character::fromJson(std::string_view text, std::string* error)
{
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object())
    {
        if (error)
            *error = "not valid JSON";
        return std::nullopt;
    }
    try
    {
        if (j.value("version", 0) > sheetVersion)
        {
            if (error) *error = "sheet was saved by a newer version";
            return std::nullopt;
        }
        Character c;
        c.name = j.value("name", "");
        c.ancestry = j.value("ancestry", "");
        c.characterClass = j.value("class", "");
        c.level = j.value("level", 1);
        if (c.level < 1 || c.level > 1000) throw std::invalid_argument("Sheet level must be 1 to 1000");
        c.xp = j.value("xp", 0);
        c.hitDie = j.value("hitDie", "1d8");
        c.hp = j.value("hp", 0);
        c.tempHp = j.value("tempHp", 0);
        if (j.contains("death"))
        {
            const auto& death_ = j.at("death");
            if (!death_.is_object()) throw std::invalid_argument("Sheet death state must be an object");
            c.death.saves = death_.value("saves", c.death.saves);
            c.death.successes = death_.value("successes", 0); c.death.failures = death_.value("failures", 0);
            c.death.stable = death_.value("stable", false); c.death.dead = death_.value("dead", false);
            if (c.death.successes < 0 || c.death.successes > 100 || c.death.failures < 0 || c.death.failures > 100
                || (c.death.stable && c.death.dead) || ((c.death.stable || c.death.dead) && c.hp > 0))
                throw std::invalid_argument("Invalid sheet death state");
        }
        c.notes = j.value("notes", "");
        if (j.contains("proficiencies"))
            c.proficiencies = j["proficiencies"].get<std::set<std::string>>();
        c.dcAbility = j.value("dcAbility", std::string{});
        if (c.dcAbility.size() > 64) throw std::invalid_argument("DC ability is too long");
        if (j.contains("proficiencyRanks"))
        {
            const auto& ranks = j.at("proficiencyRanks");
            if (!ranks.is_object() || ranks.size() > 1000) throw std::invalid_argument("Proficiency ranks must be an object");
            c.proficiencyRanks = ranks.get<std::map<std::string, std::string>>();
            for (const auto& [target, rank] : c.proficiencyRanks)
                if (target.empty() || target.size() > 64 || rank.empty() || rank.size() > 64) throw std::invalid_argument("Invalid proficiency choice");
        }
        if (j.contains("stats"))
        {
            for (const auto& [stat, value] : j["stats"].items())
                c.stats.setBase(stat, value.get<float>());
        }
        if (j.contains("modifiers"))
        {
            for (const json& m : j["modifiers"])
                c.stats.addModifier(modifierFrom(m));
        }
        if (j.contains("resources"))
        {
            for (const auto& [id, r] : j["resources"].items())
                c.resources[id] = {r.value("current", 0), r.value("max", 0)};
        }
        if (j.contains("inventory"))
        {
            for (const json& i : j["inventory"])
            {
                Item item;
                item.id = i.value("id", "");
                item.name = i.value("name", "");
                item.slot = i.value("slot", "");
                item.damage = i.value("damage", "");
                item.attackAbility = i.value("attackAbility", "str");
                item.hands = std::clamp(i.value("hands", 1), 0, 4);
                item.weight = i.value("weight", 0.0f);
                item.value = i.value("value", 0);
                item.quantity = i.value("quantity", 1);
                item.magic = i.value("magic", false);
                item.equipped = i.value("equipped", false);
                if (i.contains("use"))
                {
                    std::string problem;
                    const auto parsed = Compendium::itemFromJson(i.dump(), &problem);
                    if (!parsed) throw std::invalid_argument("Saved consumable: " + problem);
                    item.use = parsed->use;
                }
                if (i.contains("modifiers"))
                {
                    for (const json& m : i["modifiers"])
                        item.modifiers.push_back(modifierFrom(m));
                }
                c.inventory.push_back(std::move(item));
            }
        }
        c.coins = j.value("coins", 0);
        if (c.coins < 0) throw std::invalid_argument("Coins can't be negative");
        if (j.contains("conditions"))
        {
            for (const json& cond : j["conditions"])
                c.conditions.push_back({cond.value("id", ""), cond.value("roundsLeft", -1), std::max(1, cond.value("value", 1))});
        }
        return c;
    }
    catch (const std::exception& e)
    {
        if (error)
            *error = e.what();
        return std::nullopt;
    }
}

}
